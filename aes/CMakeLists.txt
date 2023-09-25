add_library(aes aes.c aes.h)

target_include_directories(aes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
