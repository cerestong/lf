cmake_minimum_required(VERSION 2.6.2)

project( lf-distribution )
set (CMAKE_BUILD_TYPE Debug)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
    ${SOURCE_DIR}/include
    ${SOURCE_DIR}/src
)
link_directories(
)

set(HEADER
    include/lf/compiler.hh
    include/lf/alloc_pin.hh
    include/lf/bit_util.hh
    include/lf/dyn_array.hh
    include/lf/hash_map.hh
    include/lf/hash.hh
    include/lf/time_util.hh
    include/lf/env_util.hh
    include/lf/logger.hh
    include/lf/decimal.hh
    include/lf/dtoa.hh
    include/lf/itoa.hh
    include/lf/my_decimal.hh
    include/lf/slice.hh
    include/lf/status.hh
    include/lf/auto_roll_logger.hh
    include/lf/time.hh
    include/lf/limbo.hh
    include/lf/wfmcas.hh
    include/lf/lf.hh
    #posix
    include/lf/posix_logger.hh
)

set(SOURCE
    src/alloc_pin.cc
    src/bit_util.cc
    src/dyn_array.cc
    src/hash_map.cc
    src/hash.cc
    src/logger.cc
    src/decimal.cc
    src/dtoa.cc
    src/itoa.cc
    src/my_decimal.cc
    src/slice.cc
    src/status.cc
    src/auto_roll_logger.cc
    src/time.cc
    src/limbo.cc
    src/wfmcas.cc
    src/lf.cc
    #posix
    src/posix_time_util.cc
    src/posix_env_util.cc
    src/posix_logger.cc
)

add_executable(lf-test ${HEADER} ${SOURCE} main.cc)
target_compile_options( lf-test PUBLIC -std=c++11 -w -Wall )
target_link_libraries(lf-test pthread)

add_executable(test-std-map test_std_map.cc)
target_compile_options( test-std-map PUBLIC -std=c++11 -w -Wall )
target_link_libraries(test-std-map pthread)

add_executable(test-limbo ${HEADER} ${SOURCE} test/limbo-test.cc)
target_compile_options( test-limbo PUBLIC -std=c++11 -w -Wall )
target_link_libraries(test-limbo pthread)

add_executable(test-odd ${HEADER} ${SOURCE} test/odd_test.cc)
target_compile_options( test-odd PUBLIC -std=c++11 -w -Wall )
target_link_libraries(test-odd pthread)

add_executable(test-wfmcas ${HEADER} ${SOURCE} test/wfmcas-test.cc)
target_compile_options( test-wfmcas PUBLIC -std=c++11 -w -Wall )
target_link_libraries(test-wfmcas pthread)
