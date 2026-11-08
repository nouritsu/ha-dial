file(GLOB_RECURSE sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

idf_component_register(
  SRCS ${sources}
  INCLUDE_DIRS .
  REQUIRES driver esp_lcd esp_wifi esp_netif esp_event nvs_flash json mbedtls esp_timer esp_http_client
)

# ha talks to Home Assistant, tapo to the bulbs
set(DIAL_BACKEND ha CACHE STRING "a file name in src/backends")
file(GLOB backends RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/backends ${CMAKE_CURRENT_SOURCE_DIR}/backends/*.c)
list(TRANSFORM backends REPLACE "\\.c$" "")
if(NOT DIAL_BACKEND IN_LIST backends)
  message(FATAL_ERROR "DIAL_BACKEND must be one of ${backends}, not ${DIAL_BACKEND}")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE DIAL_BACKEND=${DIAL_BACKEND}_backend)
