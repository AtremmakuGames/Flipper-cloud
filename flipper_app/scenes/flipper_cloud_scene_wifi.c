#include "../flipper_cloud.h"

enum {
    WifiItemScan,
    WifiItemManual,
    WifiItemReconnect,
};

static void flipper_cloud_scene_wifi_callback(void* context, uint32_t index) {
    FlipperCloudApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipper_cloud_scene_wifi_on_enter(void* context) {
    FlipperCloudApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "WiFi");
    submenu_add_item(
        submenu, "Scan networks", WifiItemScan, flipper_cloud_scene_wifi_callback, app);
    submenu_add_item(
        submenu, "Enter SSID manually", WifiItemManual, flipper_cloud_scene_wifi_callback, app);
    if(app->ssid[0]) {
        FuriString* label = furi_string_alloc_printf("Reconnect: %s", app->ssid);
        submenu_add_item(
            submenu,
            furi_string_get_cstr(label),
            WifiItemReconnect,
            flipper_cloud_scene_wifi_callback,
            app);
        furi_string_free(label);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewSubmenu);
}

bool flipper_cloud_scene_wifi_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case WifiItemScan:
        app->op = CloudOpScan;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        return true;
    case WifiItemManual:
        strlcpy(app->pending_ssid, app->ssid, sizeof(app->pending_ssid));
        app->input_target = CloudInputSsid;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneInput);
        return true;
    case WifiItemReconnect:
        strlcpy(app->pending_ssid, app->ssid, sizeof(app->pending_ssid));
        strlcpy(app->pending_password, app->password, sizeof(app->pending_password));
        app->op = CloudOpConnect;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        return true;
    default:
        return false;
    }
}

void flipper_cloud_scene_wifi_on_exit(void* context) {
    FlipperCloudApp* app = context;
    submenu_reset(app->submenu);
}
