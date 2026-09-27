#include "../flipper_cloud.h"

static void flipper_cloud_scene_result_button_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    FlipperCloudApp* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, CloudEventResultOk);
    }
}

void flipper_cloud_scene_result_on_enter(void* context) {
    FlipperCloudApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);
    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, app->op_success ? "Done" : "Error");
    widget_add_text_box_element(
        widget,
        0,
        14,
        128,
        36,
        AlignCenter,
        AlignCenter,
        furi_string_get_cstr(app->op_message),
        false);
    widget_add_button_element(
        widget, GuiButtonTypeCenter, "OK", flipper_cloud_scene_result_button_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewWidget);
}

static bool flipper_cloud_scene_result_leave(FlipperCloudApp* app) {
    if(!scene_manager_search_and_switch_to_previous_scene(
           app->scene_manager, FlipperCloudSceneStart)) {
        // Shown before the main menu (e.g. the USART was busy): exit the app.
        scene_manager_stop(app->scene_manager);
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

bool flipper_cloud_scene_result_on_event(void* context, SceneManagerEvent event) {
    FlipperCloudApp* app = context;

    if(event.type == SceneManagerEventTypeBack) {
        return flipper_cloud_scene_result_leave(app);
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == CloudEventResultOk) {
        return flipper_cloud_scene_result_leave(app);
    }
    return false;
}

void flipper_cloud_scene_result_on_exit(void* context) {
    FlipperCloudApp* app = context;
    widget_reset(app->widget);
}
