cmake_minimum_required(VERSION 3.21)
project(can_tp C)

set(CMAKE_C_STANDARD 11)

include_directories(src)

add_executable(can_tp
        src/cantp.c
        src/cantp.h
        example/can_gcc/main.c)

include(CTest)

if(BUILD_TESTING)
    add_executable(cantp_rx_bounds_test
            src/cantp.c
            tests/test_rx_bounds.c)
    add_test(NAME cantp_rx_bounds COMMAND cantp_rx_bounds_test)
endif()
