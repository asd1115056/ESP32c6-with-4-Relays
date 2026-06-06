idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_netif nvs_flash
                             esp_event lwip bt esp_driver_gpio)
