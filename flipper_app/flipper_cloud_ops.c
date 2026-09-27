/**
 * Operations executed in the worker thread. They talk to the ESP32 using the
 * line based protocol described in docs/PROTOCOL.md.
 */
#include "flipper_cloud.h"

#include <toolbox/path.h>

#define PING_TIMEOUT_MS       1500
#define STATUS_TIMEOUT_MS     3000
#define SCAN_TIMEOUT_MS       20000
#define CONNECT_TIMEOUT_MS    30000
#define HTTP_TIMEOUT_MS       30000
#define CHUNK_LINE_TIMEOUT_MS 20000
#define RAW_TIMEOUT_MS        5000
#define ABORT_TIMEOUT_MS      10000
#define MAX_CHUNK_SIZE        1024

typedef struct {
    const char* ext;
    const char* dir;
} CloudAutoFolder;

static const CloudAutoFolder auto_folders[] = {
    {".sub", EXT_PATH("subghz")},
    {".ir", EXT_PATH("infrared")},
    {".nfc", EXT_PATH("nfc")},
    {".rfid", EXT_PATH("lfrfid")},
    {".ibtn", EXT_PATH("ibutton")},
    {".fmf", EXT_PATH("music_player")},
    {".js", EXT_PATH("apps/Scripts")},
    {".fap", EXT_PATH("apps/Misc")},
};

static void set_status(FlipperCloudApp* app, const char* status) {
    progress_view_set_status(app->progress, status);
}

static void fail(FlipperCloudApp* app, const char* message) {
    app->op_success = false;
    furi_string_set(app->op_message, message);
}

/** Reads a line, giving up on timeout or (when `cancellable`) on user cancel. */
static bool
    read_line(FlipperCloudApp* app, FuriString* line, uint32_t timeout_ms, bool cancellable) {
    furi_string_reset(line);
    uint32_t start = furi_get_tick();
    while(!(cancellable && app->cancel)) {
        if(cloud_uart_read_line(app->uart, line, 100)) return true;
        if(furi_get_tick() - start > furi_ms_to_ticks(timeout_ms)) return false;
    }
    return false;
}

/**
 * Waits for a line that starts with one of `prefixes` (NULL terminated list).
 * Other lines (boot messages, leftovers) are skipped. Returns the index of the
 * matched prefix, or -1 on timeout/cancel. `args` receives the text after the
 * prefix and the tab that follows it.
 */
static int wait_reply(
    FlipperCloudApp* app,
    const char* const* prefixes,
    FuriString* args,
    uint32_t timeout_ms,
    bool cancellable) {
    FuriString* line = furi_string_alloc();
    uint32_t start = furi_get_tick();
    int found = -1;

    while(found < 0) {
        uint32_t elapsed = (furi_get_tick() - start) / furi_ms_to_ticks(1);
        if(elapsed >= timeout_ms) break;
        if(!read_line(app, line, timeout_ms - elapsed, cancellable)) break;

        for(int i = 0; prefixes[i]; i++) {
            size_t length = strlen(prefixes[i]);
            const char* text = furi_string_get_cstr(line);
            if(strncmp(text, prefixes[i], length) == 0 &&
               (text[length] == '\0' || text[length] == '\t')) {
                furi_string_set(args, text[length] ? text + length + 1 : "");
                found = i;
                break;
            }
        }
    }

    furi_string_free(line);
    return found;
}

/** Splits "a\tb\tc" style arguments: returns the part before the first tab and advances. */
static void next_field(FuriString* args, FuriString* field) {
    size_t tab = furi_string_search_char(args, '\t');
    if(tab == FURI_STRING_FAILURE) {
        furi_string_set(field, args);
        furi_string_reset(args);
    } else {
        furi_string_set_n(field, args, 0, tab);
        furi_string_right(args, tab + 1);
    }
}

static void send_command(FlipperCloudApp* app, FuriString* command) {
    cloud_uart_flush_rx(app->uart);
    cloud_uart_send_line(app->uart, furi_string_get_cstr(command));
}

static bool ensure_module(FlipperCloudApp* app) {
    static const char* const pong[] = {"PONG", NULL};
    FuriString* args = furi_string_alloc();
    bool found = false;

    set_status(app, "Looking for module...");
    for(int attempt = 0; attempt < 3 && !found && !app->cancel; attempt++) {
        cloud_uart_flush_rx(app->uart);
        cloud_uart_send_line(app->uart, "PING");
        found = wait_reply(app, pong, args, PING_TIMEOUT_MS, true) == 0;
    }
    if(found)
        strlcpy(app->module_version, furi_string_get_cstr(args), sizeof(app->module_version));

    if(!found && !app->cancel) {
        fail(
            app,
            "WiFi module not responding.\nFlash the Flipper Cloud firmware\nand check the connection.");
    }
    furi_string_free(args);
    return found;
}

static bool wifi_connect(FlipperCloudApp* app, const char* ssid, const char* password) {
    static const char* const replies[] = {"OK", "ERR", NULL};
    FuriString* command = furi_string_alloc_printf("CONNECT\t%s\t%s", ssid, password);
    FuriString* args = furi_string_alloc();

    set_status(app, "Connecting to WiFi...");
    send_command(app, command);
    int reply = wait_reply(app, replies, args, CONNECT_TIMEOUT_MS, true);

    bool connected = reply == 0;
    if(connected) {
        strlcpy(app->ip, furi_string_get_cstr(args), sizeof(app->ip));
    } else if(reply == 1) {
        furi_string_printf(app->op_message, "WiFi error:\n%s", furi_string_get_cstr(args));
        app->op_success = false;
    } else if(!app->cancel) {
        fail(app, "No answer from module\nwhile connecting to WiFi.");
    }

    furi_string_free(command);
    furi_string_free(args);
    return connected;
}

/** Makes sure the module answers and is online, reconnecting with saved credentials. */
static bool ensure_wifi(FlipperCloudApp* app) {
    static const char* const replies[] = {"OK", "ERR", NULL};
    if(!ensure_module(app)) return false;

    FuriString* args = furi_string_alloc();
    cloud_uart_send_line(app->uart, "STATUS");
    bool online = wait_reply(app, replies, args, STATUS_TIMEOUT_MS, true) == 0;
    if(online) strlcpy(app->ip, furi_string_get_cstr(args), sizeof(app->ip));
    furi_string_free(args);

    if(online || app->cancel) return online;
    if(app->ssid[0] == '\0') {
        fail(app, "WiFi is not set up.\nOpen \"WiFi\" in the menu first.");
        return false;
    }
    return wifi_connect(app, app->ssid, app->password);
}

static void op_ping(FlipperCloudApp* app) {
    static const char* const replies[] = {"OK", "ERR", NULL};
    if(!ensure_module(app)) return;

    FuriString* args = furi_string_alloc();
    cloud_uart_send_line(app->uart, "STATUS");
    bool online = wait_reply(app, replies, args, STATUS_TIMEOUT_MS, true) == 0;
    if(online) strlcpy(app->ip, furi_string_get_cstr(args), sizeof(app->ip));
    furi_string_free(args);
    if(app->cancel) return;

    if(!online && app->ssid[0] && !wifi_connect(app, app->ssid, app->password)) {
        // The module works, only WiFi failed: keep the WiFi error but say so.
        furi_string_printf(
            app->op_message, "Module OK, but\n%s", furi_string_get_cstr(app->op_message));
        return;
    }

    app->op_success = true;
    if(app->ssid[0]) {
        furi_string_printf(
            app->op_message, "%s\nOnline: %s\nIP %s", app->module_version, app->ssid, app->ip);
    } else {
        furi_string_printf(app->op_message, "%s\nWiFi is not set up yet.", app->module_version);
    }
}

static void op_scan(FlipperCloudApp* app) {
    static const char* const replies[] = {"AP", "SCAN_END", "ERR", NULL};
    if(!ensure_module(app)) return;

    set_status(app, "Scanning networks...");
    FuriString* args = furi_string_alloc();
    FuriString* field = furi_string_alloc();
    cloud_uart_send_line(app->uart, "SCAN");
    app->network_count = 0;

    while(true) {
        int reply = wait_reply(app, replies, args, SCAN_TIMEOUT_MS, true);
        if(reply == 0) {
            if(app->network_count >= CLOUD_MAX_NETWORKS) continue;
            CloudNetwork* network = &app->networks[app->network_count];
            next_field(args, field);
            network->rssi = (int8_t)atoi(furi_string_get_cstr(field));
            next_field(args, field);
            network->open = furi_string_equal_str(field, "1");
            next_field(args, field);
            network->channel = (uint8_t)atoi(furi_string_get_cstr(field));
            strlcpy(network->ssid, furi_string_get_cstr(args), sizeof(network->ssid));
            if(network->ssid[0]) app->network_count++;
        } else if(reply == 1) {
            app->op_success = true;
            break;
        } else if(reply == 2) {
            furi_string_printf(app->op_message, "Scan failed:\n%s", furi_string_get_cstr(args));
            break;
        } else {
            if(!app->cancel) fail(app, "Scan timed out.");
            break;
        }
    }

    furi_string_free(args);
    furi_string_free(field);
}

static void op_connect(FlipperCloudApp* app) {
    if(!ensure_module(app)) return;
    if(!wifi_connect(app, app->pending_ssid, app->pending_password)) return;

    strlcpy(app->ssid, app->pending_ssid, sizeof(app->ssid));
    strlcpy(app->password, app->pending_password, sizeof(app->password));
    flipper_cloud_settings_save(app);

    app->op_success = true;
    furi_string_printf(app->op_message, "Connected to\n%s\nIP %s", app->ssid, app->ip);
}

static void op_list(FlipperCloudApp* app) {
    static const char* const replies[] = {"FILE", "OK", "ERR", NULL};
    if(!ensure_wifi(app)) return;

    set_status(app, "Reading bin...");
    FuriString* command = furi_string_alloc_printf("LIST\t%s", app->bin);
    FuriString* args = furi_string_alloc();
    FuriString* field = furi_string_alloc();
    send_command(app, command);
    app->file_count = 0;

    while(true) {
        int reply = wait_reply(app, replies, args, HTTP_TIMEOUT_MS, true);
        if(reply == 0) {
            if(app->file_count >= CLOUD_MAX_FILES) continue;
            CloudFile* file = &app->files[app->file_count];
            next_field(args, field);
            file->size = strtoul(furi_string_get_cstr(field), NULL, 10);
            strlcpy(file->name, furi_string_get_cstr(args), sizeof(file->name));
            if(file->name[0]) app->file_count++;
        } else if(reply == 1) {
            app->op_success = true;
            break;
        } else if(reply == 2) {
            furi_string_printf(app->op_message, "Filebin error:\n%s", furi_string_get_cstr(args));
            break;
        } else {
            if(!app->cancel) fail(app, "Filebin did not answer.");
            break;
        }
    }

    furi_string_free(command);
    furi_string_free(args);
    furi_string_free(field);
}

static void op_upload(FlipperCloudApp* app) {
    static const char* const replies[] = {"NEXT", "OK", "ERR", NULL};
    if(!ensure_wifi(app)) return;

    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(
           file, furi_string_get_cstr(app->file_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        fail(app, "Cannot open the file.");
        storage_file_free(file);
        return;
    }

    uint32_t size = storage_file_size(file);
    uint32_t sent = 0;
    uint8_t* buffer = malloc(MAX_CHUNK_SIZE);
    FuriString* command =
        furi_string_alloc_printf("PUT\t%s\t%s\t%lu", app->bin, app->remote_name, size);
    FuriString* args = furi_string_alloc();

    set_status(app, "Connecting to filebin...");
    progress_view_set_progress(app->progress, 0, size);
    send_command(app, command);

    bool aborted = false;
    while(true) {
        uint32_t timeout = sent == 0 ? HTTP_TIMEOUT_MS : CHUNK_LINE_TIMEOUT_MS;
        // After a cancel keep listening (not cancellable) so the module finishes cleanly.
        int reply = wait_reply(app, replies, args, aborted ? ABORT_TIMEOUT_MS : timeout, false);

        if(reply == 0) {
            if(app->cancel) {
                // Stop feeding data: the module times out and reports ERR.
                aborted = true;
                set_status(app, "Cancelling...");
                continue;
            }
            uint32_t want = strtoul(furi_string_get_cstr(args), NULL, 10);
            want = MIN(MIN(want, (uint32_t)MAX_CHUNK_SIZE), size - sent);
            size_t got = storage_file_read(file, buffer, want);
            if(got != want) {
                // Keep the byte count promised to the server; it will be rejected anyway.
                memset(buffer + got, 0, want - got);
            }
            cloud_uart_send(app->uart, buffer, want);
            sent += want;
            set_status(app, "Uploading...");
            progress_view_set_progress(app->progress, sent, size);
        } else if(reply == 1) {
            if(aborted) {
                fail(app, "Cancelled.");
            } else {
                app->op_success = true;
                furi_string_printf(
                    app->op_message,
                    "Uploaded %s\nto bin %s\n(%lu bytes)",
                    app->remote_name,
                    app->bin,
                    size);
            }
            break;
        } else if(reply == 2) {
            if(aborted) {
                fail(app, "Cancelled.");
            } else {
                furi_string_printf(
                    app->op_message, "Upload failed:\n%s", furi_string_get_cstr(args));
            }
            break;
        } else {
            fail(app, aborted ? "Cancelled." : "Module stopped answering.");
            break;
        }
    }

    furi_string_free(command);
    furi_string_free(args);
    free(buffer);
    storage_file_close(file);
    storage_file_free(file);
}

/** Replaces characters that are not safe for the SD card file system. */
static void sanitize_name(const char* name, FuriString* out) {
    furi_string_reset(out);
    for(const char* c = name; *c; c++) {
        bool safe = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                    (*c >= '0' && *c <= '9') || strchr(" ._-()+,=[]", *c);
        furi_string_push_back(out, safe ? *c : '_');
    }
    if(furi_string_empty(out)) furi_string_set(out, "file");
}

static void build_download_path(FlipperCloudApp* app, const char* name, FuriString* path) {
    const char* dir = CLOUD_DOWNLOAD_DIR;
    const char* ext = strrchr(name, '.');
    if(app->save_mode == CloudSaveAuto && ext) {
        for(size_t i = 0; i < COUNT_OF(auto_folders); i++) {
            if(strcasecmp(ext, auto_folders[i].ext) == 0) {
                dir = auto_folders[i].dir;
                break;
            }
        }
    }
    storage_simply_mkdir(app->storage, dir);

    FuriString* safe = furi_string_alloc();
    sanitize_name(name, safe);
    furi_string_printf(path, "%s/%s", dir, furi_string_get_cstr(safe));

    // Never overwrite an existing file: add _1, _2, ... before the extension.
    FuriString* stem = furi_string_alloc();
    FuriString* suffix = furi_string_alloc();
    size_t dot = furi_string_search_rchar(safe, '.');
    if(dot == FURI_STRING_FAILURE || dot == 0) dot = furi_string_size(safe);
    furi_string_set_n(stem, safe, 0, dot);
    furi_string_set_n(suffix, safe, dot, furi_string_size(safe) - dot);
    for(int i = 1; i < 100 && storage_common_exists(app->storage, furi_string_get_cstr(path));
        i++) {
        furi_string_printf(
            path, "%s/%s_%d%s", dir, furi_string_get_cstr(stem), i, furi_string_get_cstr(suffix));
    }

    furi_string_free(stem);
    furi_string_free(suffix);
    furi_string_free(safe);
}

static void op_download(FlipperCloudApp* app) {
    static const char* const replies[] = {"SIZE", "DATA", "OK", "ERR", NULL};
    if(!ensure_wifi(app)) return;

    build_download_path(app, app->remote_name, app->file_path);
    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(
           file, furi_string_get_cstr(app->file_path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        fail(app, "Cannot create the file\non the SD card.");
        storage_file_free(file);
        return;
    }

    uint8_t* buffer = malloc(MAX_CHUNK_SIZE);
    FuriString* command = furi_string_alloc_printf("GET\t%s\t%s", app->bin, app->remote_name);
    FuriString* args = furi_string_alloc();
    uint32_t total = 0;
    uint32_t received = 0;
    bool ok = false;
    bool aborted = false;

    set_status(app, "Connecting to filebin...");
    send_command(app, command);

    while(true) {
        uint32_t timeout = received == 0 ? HTTP_TIMEOUT_MS : CHUNK_LINE_TIMEOUT_MS;
        int reply = wait_reply(app, replies, args, aborted ? ABORT_TIMEOUT_MS : timeout, false);

        if(reply == 0) {
            long size = strtol(furi_string_get_cstr(args), NULL, 10);
            total = size > 0 ? (uint32_t)size : 0;
            set_status(app, "Downloading...");
            progress_view_set_progress(app->progress, 0, total);
        } else if(reply == 1) {
            uint32_t length = strtoul(furi_string_get_cstr(args), NULL, 10);
            if(length > MAX_CHUNK_SIZE ||
               !cloud_uart_read_exact(app->uart, buffer, length, RAW_TIMEOUT_MS)) {
                fail(app, "Transfer error (UART).");
                break;
            }
            if(storage_file_write(file, buffer, length) != length) {
                cloud_uart_send_line(app->uart, "CANCEL");
                fail(app, "SD card write failed.");
                aborted = true;
                continue;
            }
            received += length;
            progress_view_set_progress(app->progress, received, total);
            if(app->cancel && !aborted) {
                aborted = true;
                fail(app, "Cancelled.");
                set_status(app, "Cancelling...");
            }
            cloud_uart_send_line(app->uart, aborted ? "CANCEL" : "ACK");
        } else if(reply == 2) {
            ok = !aborted;
            break;
        } else if(reply == 3) {
            if(!aborted) {
                furi_string_printf(
                    app->op_message, "Download failed:\n%s", furi_string_get_cstr(args));
            }
            break;
        } else {
            if(!aborted) fail(app, "Module stopped answering.");
            break;
        }
    }

    storage_file_close(file);
    storage_file_free(file);

    if(ok) {
        app->op_success = true;
        furi_string_printf(
            app->op_message,
            "Saved to\n%s\n(%lu bytes)",
            furi_string_get_cstr(app->file_path),
            received);
    } else {
        storage_simply_remove(app->storage, furi_string_get_cstr(app->file_path));
        if(app->cancel && !app->op_success && furi_string_empty(app->op_message)) {
            fail(app, "Cancelled.");
        }
    }

    furi_string_free(command);
    furi_string_free(args);
    free(buffer);
}

int32_t flipper_cloud_op_worker(void* context) {
    FlipperCloudApp* app = context;
    app->op_success = false;
    furi_string_reset(app->op_message);

    switch(app->op) {
    case CloudOpPing:
        op_ping(app);
        break;
    case CloudOpScan:
        op_scan(app);
        break;
    case CloudOpConnect:
        op_connect(app);
        break;
    case CloudOpList:
        op_list(app);
        break;
    case CloudOpUpload:
        op_upload(app);
        break;
    case CloudOpDownload:
        op_download(app);
        break;
    }

    if(!app->op_success && furi_string_empty(app->op_message)) {
        furi_string_set(app->op_message, app->cancel ? "Cancelled." : "Unknown error.");
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, CloudEventOpDone);
    return 0;
}
