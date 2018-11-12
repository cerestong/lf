cmake_minimum_required(VERSION 2.6.2)

project( lf-distribution )
set (CMAKE_BUILD_TYPE Debug)

#gtest
#SET(GTEST_ROOT /home/tongxingguo/work/install)
#find_package(GTest)
#if(GTEST_FOUND)
#    enable_testing()
#    message("GTest found, enable_testing")
#    include_directories(${GTEST_INCLUDE_DIRS})
#endif()

#rocksdb
#SET(ROCKSDB_LIBRARIES /home/tongxingguo/work/install/lib/librocksdb.a)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# x86 unaligned access available
add_definitions(-DHAVE_UNALIGNED_ACCESS)

include(cmake/internal_utils.cmake)
config_compiler_and_linker()

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
    include/lf/masstree.hh
    #posix
    include/lf/posix_logger.hh
    include/lf/random.hh
)

set(SOURCE
    src/compiler.cc
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
    src/random.cc
    src/masstree/mt_key.cc
)

LIST(APPEND LF_DEF
    pthread
)

#add_executable(wfmcas-test ${HEADER} ${SOURCE} test/wfmcas-test.cc)
#target_compile_options( wfmcas-test PUBLIC -std=c++11 -w -Wall )
#target_link_libraries(wfmcas-test pthread)

cxx_shared_library(lf "${cxx_strict}" "${LF_DEF}" "${SOURCE}")

cxx_executable(lf-test "test" "lf")
cxx_executable(test-std-map "test" "lf")
cxx_executable(limbo-test "test" "lf")
cxx_executable(odd_test "test" "lf")
cxx_executable(wfmcas-test "test" "lf")
cxx_executable(unit-mt-test "test" "lf")
#cxx_executable(rocksdb-option-test "test" "lf;${ROCKSDB_LIBRARIES};pthread;snappy;z;bz2;lz4")

IF(GTEST_FOUND)
    cxx_test(gtest-mtkey "lf;${GTEST_BOTH_LIBRARIES};${CMAKE_THREAD_LIBS_INIT}")
    cxx_test(gtest-mtstruct "lf;${GTEST_BOTH_LIBRARIES};${CMAKE_THREAD_LIBS_INIT}")
ENDIF()

