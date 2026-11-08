file(GLOB_RECURSE sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

idf_component_register(
  SRCS ${sources}
  INCLUDE_DIRS .
  REQUIRES driver esp_lcd esp_wifi esp_netif esp_event nvs_flash json mbedtls esp_timer esp_http_client
)
