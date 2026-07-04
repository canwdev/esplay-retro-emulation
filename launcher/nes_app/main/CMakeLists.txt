idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nofrendo hal-drivers nvs_flash app_update esp_timer)

target_compile_definitions(${COMPONENT_LIB} PUBLIC TARGET_ESP32)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -Wno-char-subscripts
    -Wno-array-bounds
    -Wno-error=format
    -Wno-format
    -mfix-esp32-psram-cache-issue
    -mfix-esp32-psram-cache-strategy=memw)
