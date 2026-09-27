#include "../flipper_cloud.h"

static const char* help_text = "\e#Flipper Cloud\n"
                               "Sync files with filebin.net\n"
                               "through the ESP32 WiFi board.\n"
                               "\n"
                               "\e#Setup\n"
                               "1. Flash FlipperCloudESP32\n"
                               "firmware to the WiFi board.\n"
                               "2. WiFi -> Scan networks,\n"
                               "pick yours, type password.\n"
                               "3. Bin -> enter a filebin\n"
                               "code (8+ chars: a-z 0-9 - _).\n"
                               "\n"
                               "\e#Upload\n"
                               "Pick any file on the SD card.\n"
                               "It appears at\n"
                               "filebin.net/<bin>/<name>\n"
                               "\n"
                               "\e#Download\n"
                               "Pick a file from the bin.\n"
                               "Auto mode stores .sub .ir .nfc\n"
                               ".rfid .ibtn .fmf .js .fap in\n"
                               "their app folders, other files\n"
                               "go to /ext/cloud_sync.\n"
                               "\n"
                               "\e#Keyboard\n"
                               "OK - type, hold OK - capital\n"
                               "Back - erase, hold Back - exit\n"
                               "Aa - case, #? - symbols\n"
                               "\n"
                               "Bins are public and expire\n"
                               "after a week. Do not store\n"
                               "secrets there.";

void flipper_cloud_scene_help_on_enter(void* context) {
    FlipperCloudApp* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, help_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, CloudViewWidget);
}

bool flipper_cloud_scene_help_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void flipper_cloud_scene_help_on_exit(void* context) {
    FlipperCloudApp* app = context;
    widget_reset(app->widget);
}
