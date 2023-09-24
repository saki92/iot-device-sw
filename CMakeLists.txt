cmake_minimum_required(VERSION 3.10.2)

project(MotorController)

set(SOURCES
  server.c
)

add_executable(motor-ctrl ${SOURCES})

target_link_libraries(motor-ctrl PRIVATE -lgpiod spi)

add_subdirectory(spi_device)
