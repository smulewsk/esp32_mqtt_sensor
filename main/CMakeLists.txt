idf_component_register(
    SRCS "vl53l0x.c" "wifi.c" "adc.c" "mqtt.c" "main.c" "config.c" "ap_config.c"
    INCLUDE_DIRS "."
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
)

if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
    add_compile_definitions(PROJECT_VERSION=\"${PROJECT_VERSION}\")
endif()
