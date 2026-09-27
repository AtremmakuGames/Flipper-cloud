#include "../flipper_cloud.h"

enum {
    StartItemWifi,
    StartItemBin,
    StartItemUpload,
    StartItemDownload,
    StartItemSaveMode,
    StartItemTest,
    StartItemHelp,
};

static void flipper_cloud_scene_start_callback(void* context, uint32_t index) {
    FlipperCloudApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void start_bin_input(FlipperCloudApp* app, CloudAfterBin after) {
    app->after_bin = after;
    app->input_target = CloudInputBin;
    scene_manager_next_scene(app->scene_manager, FlipperCloudSceneInput);
}

void flipper_cloud_scene_start_on_enter(void* context) {
    FlipperCloudApp* app = context;
    Submenu* submenu = app->submenu;
    FuriString* label = furi_string_alloc();

    submenu_reset(submenu);
    submenu_set_header(submenu, "Flipper Cloud (filebin)");

    if(app->ssid[0]) {
        furi_string_printf(label, "WiFi: %s", app->ssid);
    } else {
        furi_string_set(label, "WiFi: not set");
    }
    submenu_add_item(
        submenu,
        furi_string_get_cstr(label),
        StartItemWifi,
        flipper_cloud_scene_start_callback,
        app);

    if(app->bin[0]) {
        furi_string_printf(label, "Bin: %s", app->bin);
    } else {
        furi_string_set(label, "Bin: not set");
    }
    submenu_add_item(
        submenu,
        furi_string_get_cstr(label),
        StartItemBin,
        flipper_cloud_scene_start_callback,
        app);

    submenu_add_item(
        submenu, "Upload file", StartItemUpload, flipper_cloud_scene_start_callback, app);
    submenu_add_item(
        submenu, "Download file", StartItemDownload, flipper_cloud_scene_start_callback, app);
    submenu_add_item(
        submenu,
        app->save_mode == CloudSaveAuto ? "Save to: auto by type" : "Save to: /cloud_sync",
        StartItemSaveMode,
        flipper_cloud_scene_start_callback,
        app);
    submenu_add_item(
        submenu, "Test module", StartItemTest, flipper_cloud_scene_start_callback, app);
    submenu_add_item(submenu, "Help", StartItemHelp, flipper_cloud_scene_start_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, FlipperCloudSceneStart));
    furi_string_free(label);

    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewSubmenu);
}

bool flipper_cloud_scene_start_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, FlipperCloudSceneStart, event.event);

    switch(event.event) {
    case StartItemWifi:
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneWifi);
        return true;
    case StartItemBin:
        start_bin_input(app, CloudAfterBinNothing);
        return true;
    case StartItemUpload:
        if(!app->bin[0]) {
            start_bin_input(app, CloudAfterBinUpload);
        } else if(flipper_cloud_pick_upload_file(app)) {
            app->op = CloudOpUpload;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        }
        return true;
    case StartItemDownload:
        if(!app->bin[0]) {
            start_bin_input(app, CloudAfterBinDownload);
        } else {
            app->op = CloudOpList;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        }
        return true;
    case StartItemSaveMode:
        app->save_mode = app->save_mode == CloudSaveAuto ? CloudSaveFolder : CloudSaveAuto;
        flipper_cloud_settings_save(app);
        submenu_change_item_label(
            app->submenu,
            StartItemSaveMode,
            app->save_mode == CloudSaveAuto ? "Save to: auto by type" : "Save to: /cloud_sync");
        return true;
    case StartItemTest:
        app->op = CloudOpPing;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        return true;
    case StartItemHelp:
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneHelp);
        return true;
    default:
        return false;
    }
}

void flipper_cloud_scene_start_on_exit(void* context) {
    FlipperCloudApp* app = context;
    submenu_reset(app->submenu);
}
