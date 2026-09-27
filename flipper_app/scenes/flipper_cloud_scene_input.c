#include "../flipper_cloud.h"

static const char* validate_ssid(const char* text, void* context) {
    UNUSED(context);
    return text[0] ? NULL : "SSID can't be empty";
}

static const char* validate_password(const char* text, void* context) {
    UNUSED(context);
    size_t length = strlen(text);
    // Empty is allowed for open networks, WPA needs 8..63 characters.
    return (length == 0 || (length >= 8 && length <= 63)) ? NULL : "WPA needs 8-63 chars";
}

static const char* validate_bin(const char* text, void* context) {
    UNUSED(context);
    size_t length = strlen(text);
    if(length < 8) return "Bin: min 8 characters";
    for(const char* c = text; *c; c++) {
        bool valid = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                     (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
        if(!valid) return "Bin: a-z A-Z 0-9 - _";
    }
    return NULL;
}

static void flipper_cloud_scene_input_callback(void* context) {
    FlipperCloudApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, CloudEventInputDone);
}

void flipper_cloud_scene_input_on_enter(void* context) {
    FlipperCloudApp* app = context;
    const char* header = "";
    FullKeyboardValidator validator = NULL;
    size_t size = sizeof(app->input_buffer);

    switch(app->input_target) {
    case CloudInputSsid:
        header = "WiFi name (SSID)";
        validator = validate_ssid;
        size = CLOUD_SSID_SIZE;
        strlcpy(app->input_buffer, app->pending_ssid, size);
        break;
    case CloudInputPassword:
        header = "WiFi password";
        validator = validate_password;
        size = CLOUD_PASSWORD_SIZE;
        // Offer the saved password when reconnecting to the same network.
        if(strcmp(app->pending_ssid, app->ssid) == 0) {
            strlcpy(app->input_buffer, app->password, size);
        } else {
            app->input_buffer[0] = '\0';
        }
        break;
    case CloudInputBin:
        header = "Filebin code";
        validator = validate_bin;
        size = CLOUD_BIN_SIZE;
        strlcpy(app->input_buffer, app->bin, size);
        break;
    }

    full_keyboard_setup(
        app->keyboard,
        header,
        app->input_buffer,
        size,
        validator,
        flipper_cloud_scene_input_callback,
        app);
    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewKeyboard);
}

static void input_bin_done(FlipperCloudApp* app) {
    strlcpy(app->bin, app->input_buffer, sizeof(app->bin));
    flipper_cloud_settings_save(app);

    CloudAfterBin after = app->after_bin;
    app->after_bin = CloudAfterBinNothing;

    if(after == CloudAfterBinUpload) {
        if(flipper_cloud_pick_upload_file(app)) {
            app->op = CloudOpUpload;
            scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
            return;
        }
    } else if(after == CloudAfterBinDownload) {
        app->op = CloudOpList;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        return;
    }
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, FlipperCloudSceneStart);
}

bool flipper_cloud_scene_input_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        if(app->input_target == CloudInputPassword) {
            // Go back to the list we came from (network list or WiFi menu).
            static const uint32_t scenes[] = {FlipperCloudSceneNetworks, FlipperCloudSceneWifi};
            return scene_manager_search_and_switch_to_previous_scene_one_of(
                app->scene_manager, scenes, COUNT_OF(scenes));
        }
        app->after_bin = CloudAfterBinNothing;
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom || event.event != CloudEventInputDone) {
        return false;
    }

    switch(app->input_target) {
    case CloudInputSsid:
        strlcpy(app->pending_ssid, app->input_buffer, sizeof(app->pending_ssid));
        app->input_target = CloudInputPassword;
        // Re-enter this scene with the new target instead of stacking another copy.
        flipper_cloud_scene_input_on_enter(app);
        break;
    case CloudInputPassword:
        strlcpy(app->pending_password, app->input_buffer, sizeof(app->pending_password));
        app->op = CloudOpConnect;
        scene_manager_next_scene(app->scene_manager, FlipperCloudSceneOp);
        break;
    case CloudInputBin:
        input_bin_done(app);
        break;
    }
    return true;
}

void flipper_cloud_scene_input_on_exit(void* context) {
    UNUSED(context);
}
