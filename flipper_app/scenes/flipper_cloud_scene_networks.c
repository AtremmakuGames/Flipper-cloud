#include "../flipper_cloud.h"

#define NETWORKS_RESCAN UINT32_MAX

static void flipper_cloud_scene_networks_callback(void* context, uint32_t index) {
    FlipperCloudApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipper_cloud_scene_networks_on_enter(void* context) {
    FlipperCloudApp* app = context;
    Submenu* submenu = app->submenu;
    FuriString* label = furi_string_alloc();

    submenu_reset(submenu);
    submenu_set_header(submenu, "Select network");
    for(size_t i = 0; i < app->network_count; i++) {
        const CloudNetwork* network = &app->networks[i];
        furi_string_printf(
            label, "%s%s (%d)", network->open ? "" : "* ", network->ssid, network->rssi);
        submenu_add_item(
            submenu, furi_string_get_cstr(label), i, flipper_cloud_scene_networks_callback, app);
    }
    submenu_add_item(
        submenu,
        app->network_count ? "Rescan" : "Nothing found. Rescan",
        NETWORKS_RESCAN,
        flipper_cloud_scene_networks_callback,
        app);
    furi_string_free(label);

    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewSubmenu);
}

bool flipper_cloud_scene_networks_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        // Skip the finished scan operation that is below us in the stack.
        return scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, FlipperCloudSceneWifi);
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == NETWORKS_RESCAN) {
        app->op = CloudOpScan;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, FlipperCloudSceneWifi);
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        return true;
    }

    if(event.event < app->network_count) {
        const CloudNetwork* network = &app->networks[event.event];
        strlcpy(app->pending_ssid, network->ssid, sizeof(app->pending_ssid));
        if(network->open) {
            app->pending_password[0] = '\0';
            app->op = CloudOpConnect;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        } else {
            app->input_target = CloudInputPassword;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneInput);
        }
        return true;
    }
    return false;
}

void flipper_cloud_scene_networks_on_exit(void* context) {
    FlipperCloudApp* app = context;
    submenu_reset(app->submenu);
}
