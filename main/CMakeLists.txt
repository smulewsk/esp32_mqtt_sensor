idf_component_register(
    SRCS "mqtt.c" "main.c" "config.c"             
    INCLUDE_DIRS "."
    REQUIRES 
        esp_adc
        esp_wifi 
        esp_netif 
        esp_event 
        nvs_flash
        mqtt
        esp_pm
)

if(EXISTS "${CMAKE_SOURCE_DIR}/VERSION")
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" PROJECT_VERSION)
    string(STRIP "${PROJECT_VERSION}" PROJECT_VERSION)
    add_compile_definitions(PROJECT_VERSION=\"${PROJECT_VERSION}\")
endif()
