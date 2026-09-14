idf_component_register(
    SRCS
        "app_main.c"
        "pin_scope_test.c"
        "oled_button_test.c"
        "badge_app.c"
        "badge_contacts.c"
        "badge_diagnostics.c"
        "badge_directory.c"
        "badge_hardware_info.c"
        "badge_state.c"
        "modules/buttons.c"
        "modules/buzzer.c"
        "modules/infrared.c"
        "modules/oled.c"
        "modules/rgb.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer esp_rom esp_psram heap spi_flash
)
