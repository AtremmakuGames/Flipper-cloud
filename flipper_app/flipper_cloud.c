#include "flipper_cloud.h"

#include <furi_hal_power.h>
#include <expansion/expansion.h>
#include <flipper_format/flipper_format.h>

#include <toolbox/path.h>

#define SETTINGS_FILETYPE "Flipper Cloud Settings"
#define SETTINGS_VERSION  1

static void flipper_cloud_settings_load(FlipperCloudApp* app) {
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    FuriString* value = furi_string_alloc();
    uint32_t version = 0;

    if(flipper_format_file_open_existing(ff, CLOUD_SETTINGS_PATH) &&
       flipper_format_read_header(ff, value, &version) &&
       furi_string_equal_str(value, SETTINGS_FILETYPE)) {
        if(flipper_format_read_string(ff, "SSID", value)) {
            strlcpy(app->ssid, furi_string_get_cstr(value), sizeof(app->ssid));
        }
        if(flipper_format_read_string(ff, "Password", value)) {
            strlcpy(app->password, furi_string_get_cstr(value), sizeof(app->password));
        }
        if(flipper_format_read_string(ff, "Bin", value)) {
            strlcpy(app->bin, furi_string_get_cstr(value), sizeof(app->bin));
        }
        uint32_t save_mode = CloudSaveAuto;
        if(flipper_format_read_uint32(ff, "SaveMode", &save_mode, 1)) {
            app->save_mode = save_mode == CloudSaveFolder ? CloudSaveFolder : CloudSaveAuto;
        }
    }

    furi_string_free(value);
    flipper_format_free(ff);
}

void flipper_cloud_settings_save(FlipperCloudApp* app) {
    storage_simply_mkdir(app->storage, APP_DATA_PATH(""));
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    uint32_t save_mode = app->save_mode;

    if(flipper_format_file_open_always(ff, CLOUD_SETTINGS_PATH)) {
        flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION);
        flipper_format_write_string_cstr(ff, "SSID", app->ssid);
        flipper_format_write_string_cstr(ff, "Password", app->password);
        flipper_format_write_string_cstr(ff, "Bin", app->bin);
        flipper_format_write_uint32(ff, "SaveMode", &save_mode, 1);
    }

    flipper_format_free(ff);
}

bool flipper_cloud_pick_upload_file(FlipperCloudApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, "*", NULL);
    options.base_path = STORAGE_EXT_PATH_PREFIX;
    options.hide_dot_files = true;

    // Start where the last file was, unless it is gone (e.g. a failed download).
    if(furi_string_empty(app->file_path) ||
       !storage_common_exists(app->storage, furi_string_get_cstr(app->file_path))) {
        furi_string_set(app->file_path, STORAGE_EXT_PATH_PREFIX);
    }
    if(!dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &options)) {
        return false;
    }

    FuriString* name = furi_string_alloc();
    path_extract_filename(app->file_path, name, false);
    strlcpy(app->remote_name, furi_string_get_cstr(name), sizeof(app->remote_name));
    furi_string_free(name);
    return true;
}

void flipper_cloud_show_result(FlipperCloudApp* app, bool success, const char* message) {
    app->op_success = success;
    furi_string_set(app->op_message, message);
    scene_manager_next_scene(app->scene_manager, FlipperCloudSceneResult);
}

static bool flipper_cloud_custom_event_callback(void* context, uint32_t event) {
    FlipperCloudApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool flipper_cloud_back_event_callback(void* context) {
    FlipperCloudApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static FlipperCloudApp* flipper_cloud_app_alloc(void) {
    FlipperCloudApp* app = malloc(sizeof(FlipperCloudApp));
    memset(app, 0, sizeof(FlipperCloudApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->op_message = furi_string_alloc();
    app->file_path = furi_string_alloc();
    app->networks = malloc(sizeof(CloudNetwork) * CLOUD_MAX_NETWORKS);
    app->files = malloc(sizeof(CloudFile) * CLOUD_MAX_FILES);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&flipper_cloud_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, flipper_cloud_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, flipper_cloud_back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CloudViewSubmenu, submenu_get_view(app->submenu));
    app->keyboard = full_keyboard_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CloudViewKeyboard, full_keyboard_get_view(app->keyboard));
    app->progress = progress_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, CloudViewProgress, progress_view_get_view(app->progress));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, CloudViewWidget, widget_get_view(app->widget));

    return app;
}

static void flipper_cloud_app_free(FlipperCloudApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, CloudViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, CloudViewKeyboard);
    view_dispatcher_remove_view(app->view_dispatcher, CloudViewProgress);
    view_dispatcher_remove_view(app->view_dispatcher, CloudViewWidget);
    submenu_free(app->submenu);
    full_keyboard_free(app->keyboard);
    progress_view_free(app->progress);
    widget_free(app->widget);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    free(app->networks);
    free(app->files);
    furi_string_free(app->op_message);
    furi_string_free(app->file_path);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t flipper_cloud_app(void* p) {
    UNUSED(p);

    // The expansion service listens on the same USART, take it over while we run.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    FlipperCloudApp* app = flipper_cloud_app_alloc();
    flipper_cloud_settings_load(app);

    // The WiFi dev board is powered from the 5V pin.
    if(!furi_hal_power_is_otg_enabled()) {
        app->otg_enabled_by_us = furi_hal_power_enable_otg();
    }

    app->uart = cloud_uart_alloc(CLOUD_BAUDRATE);
    if(app->uart) {
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneStart);
    } else {
        furi_string_set(app->op_message, "USART is busy.\nClose other GPIO apps\nand try again.");
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneResult);
    }

    view_dispatcher_run(app->view_dispatcher);

    if(app->uart) cloud_uart_free(app->uart);
    if(app->otg_enabled_by_us) furi_hal_power_disable_otg();

    flipper_cloud_app_free(app);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
    return 0;
}
