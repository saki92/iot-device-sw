add_library(aes aes.c aes.h)
add_executable(aes_test test.c aes.h)

target_include_directories(aes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(aes_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aes_test PRIVATE aes -lssl -lcrypto)
