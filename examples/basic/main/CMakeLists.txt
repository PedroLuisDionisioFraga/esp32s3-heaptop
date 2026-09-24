idf_component_register(SRCS "main.c" "console_settings.c" "stress_cmds.c"
                       PRIV_REQUIRES console esp_driver_uart fatfs esp_driver_usb_serial_jtag
                                     esp_usb_cdc_rom_console nvs_flash cmd_system esp_timer
                       INCLUDE_DIRS ".")
