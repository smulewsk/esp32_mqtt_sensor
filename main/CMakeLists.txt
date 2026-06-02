set(srcs "wifi.c" "adc_manager.c" "battery.c" "mqtt.c" "main.c" "config.c" "ap_config.c" "ota.c" "common.c")

list(APPEND srcs "sensors/distance_sensor.c")

if(CONFIG_VL53L0X_ENABLE)
    list(APPEND srcs "drivers/vl53l0x.c")
endif()

if(CONFIG_VL53L1X_ENABLE)
    list(APPEND srcs "drivers/vl53l1x.c")
endif()

if(CONFIG_TL136_ENABLE)
    list(APPEND srcs "drivers/tl136.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "." "drivers" "sensors"
    REQUIRES 
        esp_adc
        esp_wifi 
        esp_netif 
        esp_event 
        nvs_flash
        mqtt
        esp_pm
        driver
        esp_http_server
        esp_http_client
        esp_https_ota
)

if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
    add_compile_definitions(PROJECT_VERSION=\"${PROJECT_VERSION}\")
endif()
