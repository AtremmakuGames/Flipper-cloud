#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "cloud_uart.h"
#include "views/full_keyboard.h"
#include "views/progress_view.h"
#include "scenes/flipper_cloud_scene.h"

#define CLOUD_BAUDRATE 115200

#define CLOUD_SSID_SIZE     33
#define CLOUD_PASSWORD_SIZE 65
#define CLOUD_BIN_SIZE      61
#define CLOUD_NAME_SIZE     128

#define CLOUD_MAX_NETWORKS 24
#define CLOUD_MAX_FILES    48

#define CLOUD_SETTINGS_PATH APP_DATA_PATH("settings.txt")
#define CLOUD_DOWNLOAD_DIR  EXT_PATH("cloud_sync")

typedef enum {
    CloudViewSubmenu,
    CloudViewKeyboard,
    CloudViewProgress,
    CloudViewWidget,
} CloudView;

typedef enum {
    CloudInputSsid,
    CloudInputPassword,
    CloudInputBin,
} CloudInputTarget;

typedef enum {
    CloudOpPing,
    CloudOpScan,
    CloudOpConnect,
    CloudOpList,
    CloudOpUpload,
    CloudOpDownload,
} CloudOp;

/** What to do right after the bin code has been entered. */
typedef enum {
    CloudAfterBinNothing,
    CloudAfterBinUpload,
    CloudAfterBinDownload,
} CloudAfterBin;

typedef enum {
    CloudSaveAuto, /**< Put known file types into their app folders (subghz, nfc, ...) */
    CloudSaveFolder, /**< Always save into /ext/cloud_sync */
} CloudSaveMode;

typedef enum {
    CloudEventOpDone = 100,
    CloudEventInputDone,
    CloudEventResultOk,
} CloudEvent;

typedef struct {
    char ssid[CLOUD_SSID_SIZE];
    int8_t rssi;
    uint8_t channel; /**< 1-14: 2.4 GHz, above: 5 GHz */
    bool open;
} CloudNetwork;

typedef struct {
    char name[CLOUD_NAME_SIZE];
    uint32_t size;
} CloudFile;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Widget* widget;
    FullKeyboard* keyboard;
    ProgressView* progress;
    DialogsApp* dialogs;
    NotificationApp* notifications;
    Storage* storage;

    CloudUart* uart;
    bool otg_enabled_by_us;

    /* Saved settings */
    char ssid[CLOUD_SSID_SIZE];
    char password[CLOUD_PASSWORD_SIZE];
    char bin[CLOUD_BIN_SIZE];
    CloudSaveMode save_mode;

    /* Values being edited before they are confirmed by a successful connect */
    char pending_ssid[CLOUD_SSID_SIZE];
    char pending_password[CLOUD_PASSWORD_SIZE];
    char input_buffer[CLOUD_PASSWORD_SIZE];
    CloudInputTarget input_target;
    CloudAfterBin after_bin;

    /* Current operation */
    CloudOp op;
    FuriThread* worker;
    volatile bool cancel;
    bool op_success;
    FuriString* op_message;
    FuriString* file_path; /**< Local file to upload / downloaded file */
    char remote_name[CLOUD_NAME_SIZE];
    char ip[16];
    char module_version[32];

    CloudNetwork* networks;
    size_t network_count;
    CloudFile* files;
    size_t file_count;
} FlipperCloudApp;

void flipper_cloud_settings_save(FlipperCloudApp* app);

/** Opens the file browser. Returns true and fills app->file_path when a file was picked. */
bool flipper_cloud_pick_upload_file(FlipperCloudApp* app);

/** Shows a message screen with the given title; Back/OK returns to the main menu. */
void flipper_cloud_show_result(FlipperCloudApp* app, bool success, const char* message);

/** Worker thread body that executes app->op. Defined in flipper_cloud_ops.c */
int32_t flipper_cloud_op_worker(void* context);
