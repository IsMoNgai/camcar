idf_component_register(
  SRCS "main.cpp"
  PRIV_REQUIRES esp_http_server esp_wifi nvs_flash esp_eth
  INCLUDE_DIRS "."
  REQUIRES esp32-camera camera_setup esp_psram http_server
)
