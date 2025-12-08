set(srcs "main.c" "audio_streamer.c" "network.c" "config_tool.c" "display.c" "audio_output.c")

idf_component_register(
    SRCS ${srcs}
    PRIV_REQUIRES
        esp_driver_i2s
        esp_driver_gpio
        esp_driver_spi
        esp_driver_uart
        esp_adc
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        lwip
        fatfs
        vfs
        sdmmc
        console
        esp_lcd
        lvgl
        esp_lvgl_port
    INCLUDE_DIRS "."
)
