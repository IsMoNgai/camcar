idf_component_register(
    SRCS "http_server.cpp"
    INCLUDE_DIRS "include"
    REQUIRES camera_setup esp_http_server 
    esp_timer esp_wifi nvs_flash protocol_examples_common
    esp_eth esp-tls
)