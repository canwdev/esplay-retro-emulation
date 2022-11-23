idf_component_register(SRC_DIRS "src"
        INCLUDE_DIRS "include"
                    REQUIRES fatfs esp_adc_cal nvs_flash spiffs app_update)
