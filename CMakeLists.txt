cmake_minimum_required(VERSION 3.10.2)

project(MotorController)

# Find Paho MQTT C library
find_library(PAHO_MQTT_C_LIBRARY paho-mqtt3c REQUIRED)
find_package(json-c REQUIRED)

# Include Paho MQTT C headers
include_directories(/usr/local/include)  # Change this if your headers are in a different location

add_executable(motor-ctrl device.c timer.c)
add_executable(server server.c timer.c)

# Link the Paho MQTT C library to your executable
target_link_libraries(motor-ctrl PRIVATE -lgpiod spi aes -lssl -lcrypto ${PAHO_MQTT_C_LIBRARY} json-c)
target_link_libraries(server PRIVATE aes -lssl -lcrypto)

add_subdirectory(spi_device)
add_subdirectory(aes)
