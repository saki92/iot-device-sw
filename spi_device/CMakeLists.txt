add_library(spi spi.c spi.h)

target_include_directories(spi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
