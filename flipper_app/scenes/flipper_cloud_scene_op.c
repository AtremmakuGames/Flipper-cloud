#include "../flipper_cloud.h"

static const char* op_title(CloudOp op) {
    switch(op) {
    case CloudOpPing:
        return "Module test";
    case CloudOpScan:
        return "WiFi scan";
    case CloudOpConnect:
        return "WiFi connect";
    case CloudOpList:
        return "Filebin";
    case CloudOpUpload:
        return "Upload";
    case CloudOpDownload:
        return "Download";
    }
    return "";
}

static const char* op_subtitle(FlipperCloudApp* app) {
    switch(app->op) {
    case CloudOpConnect:
        return app->pending_ssid;
    case CloudOpList:
        return app->bin;
    case CloudOpUpload:
    case CloudOpDownload:
        return app->remote_name;
    default:
        return "";
    }
}

void flipper_cloud_scene_op_on_enter(void* context) {
    FlipperCloudApp* app = context;

    progress_view_reset(app->progress, op_title(app->op), op_subtitle(app));
    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewProgress);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);

    app->cancel = false;
    app->worker = furi_thread_alloc_ex("CloudWorker", 4 * 1024, flipper_cloud_op_worker, app);
    furi_thread_start(app->worker);
}

static void flipper_cloud_scene_op_finish(FlipperCloudApp* app) {
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    app->worker = NULL;
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);

    if(!app->op_success) {
        notification_message(app->notifications, &sequence_error);
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneResult);
        return;
    }

    switch(app->op) {
    case CloudOpScan:
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneNetworks);
        break;
    case CloudOpList:
        if(app->file_count == 0) {
            furi_string_printf(app->op_message, "Bin %s\nis empty or does not exist.", app->bin);
            app->op_success = false;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneResult);
        } else {
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneFiles);
        }
        break;
    default:
        notification_message(app->notifications, &sequence_success);
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneResult);
        break;
    }
}

bool flipper_cloud_scene_op_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        // Never leave while the worker is running: ask it to stop instead.
        if(!app->cancel) {
            app->cancel = true;
            progress_view_set_status(app->progress, "Cancelling...");
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == CloudEventOpDone) {
        flipper_cloud_scene_op_finish(app);
        return true;
    }
    return false;
}

void flipper_cloud_scene_op_on_exit(void* context) {
    FlipperCloudApp* app = context;
    furi_assert(app->worker == NULL);
}
