cmake_minimum_required(VERSION 3.10.2)

project(MotorController)

add_executable(motor-ctrl device.c timer.c)
add_executable(server server.c timer.c)

target_link_libraries(motor-ctrl PRIVATE -lgpiod spi aes -lssl -lcrypto)
target_link_libraries(server PRIVATE aes -lssl -lcrypto)

add_subdirectory(spi_device)
add_subdirectory(aes)
