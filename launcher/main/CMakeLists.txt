idf_component_register(
    SRCS
        "launcher_main.c"
        "ui_font.c"
        "fonts/ui_font_esplayfont.c"
        "ui_theme.c"
        "ui_chrome.c"
        "ui_home.c"
        "ui_settings.c"
        "ui_screen_test.c"
        "file_manager.c"
        "input_bridge.c"
        "preview/preview_registry.c"
        "preview/preview_text.c"
        "preview/preview_audio.c"
        "text_codec.c"
    INCLUDE_DIRS "." "preview"
    REQUIRES esp_timer hal-drivers lvgl nvs_flash)
