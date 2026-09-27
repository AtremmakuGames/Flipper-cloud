#include "../flipper_cloud.h"

static void flipper_cloud_scene_files_callback(void* context, uint32_t index) {
    FlipperCloudApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void flipper_cloud_scene_files_on_enter(void* context) {
    FlipperCloudApp* app = context;
    Submenu* submenu = app->submenu;
    FuriString* label = furi_string_alloc();

    submenu_reset(submenu);
    furi_string_printf(label, "Bin %s", app->bin);
    submenu_set_header(submenu, furi_string_get_cstr(label));

    for(size_t i = 0; i < app->file_count; i++) {
        const CloudFile* file = &app->files[i];
        if(file->size < 1024) {
            furi_string_printf(label, "%s (%luB)", file->name, file->size);
        } else {
            furi_string_printf(label, "%s (%luK)", file->name, (file->size + 1023) / 1024);
        }
        submenu_add_item(
            submenu, furi_string_get_cstr(label), i, flipper_cloud_scene_files_callback, app);
    }
    furi_string_free(label);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, FlipperCloudSceneFiles));
    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewSubmenu);
}

bool flipper_cloud_scene_files_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        scene_manager_set_scene_state(app->scene_manager, FlipperCloudSceneFiles, 0);
        return scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, FlipperCloudSceneStart);
    }
    if(event.type != SceneManagerEventTypeCustom || event.event >= app->file_count) return false;

    scene_manager_set_scene_state(app->scene_manager, FlipperCloudSceneFiles, event.event);
    strlcpy(app->remote_name, app->files[event.event].name, sizeof(app->remote_name));
    app->op = CloudOpDownload;
    scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
    return true;
}

void flipper_cloud_scene_files_on_exit(void* context) {
    FlipperCloudApp* app = context;
    submenu_reset(app->submenu);
}
