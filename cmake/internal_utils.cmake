macro(fix_default_compiler_settings_)
  if (MSVC)
    # For MSVC, CMake sets certain flags to defaults we want to override.
    # This replacement code is taken from sample in the CMake Wiki at
    # http://www.cmake.org/Wiki/CMake_FAQ#Dynamic_Replace.
    foreach (flag_var
             CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if (NOT BUILD_SHARED_LIBS AND NOT pi_force_shared_crt)
        # When Google Test is built as a shared library, it should also use
        # shared runtime libraries.  Otherwise, it may end up with multiple
        # copies of runtime library data in different modules, resulting in
        # hard-to-find crashes. When it is built as a static library, it is
        # preferable to use CRT as static libraries, as we don't have to rely
        # on CRT DLLs being available. CMake always defaults to using shared
        # CRT libraries, so we override that default here.
        string(REPLACE "/MD" "-MT" ${flag_var} "${${flag_var}}")
      endif()

      # We prefer more strict warning checking for building Google Test.
      # Replaces /W3 with /W4 in defaults.
      #string(REPLACE "/W3" "/W4" ${flag_var} "${${flag_var}}")
    endforeach()
  endif()
endmacro()

macro(config_compiler_and_linker)

  #fix_default_compiler_settings_()
  
  if (CMAKE_COMPILER_IS_GNUCXX)
    set(cxx_enable_0x "-std=c++11")
    set(cxx_base_flags "-Wall -Wshadow")
    set(cxx_exception_flags "-fexceptions")
    set(cxx_no_exception_flags "-fno-exceptions")
    # Until version 4.3.2, GCC doesn't define a macro to indicate
    # whether RTTI is enabled.  Therefore we define GTEST_HAS_RTTI
    # explicitly.
    set(cxx_no_rtti_flags "-fno-rtti -DLF_HAS_RTTI=0")
    set(cxx_strict_flags
      "-Wextra -Wno-unused-parameter -Wno-missing-field-initializers")
  endif()

  set(cxx_exception "${CMAKE_CXX_FLAGS} ${cxx_base_flags} ${cxx_exception_flags}")
  set(cxx_no_exception "${CMAKE_CXX_FLAGS} ${cxx_base_flags} ${cxx_no_exception_flags}")
  #set(cxx_default "${cxx_exception}")
  set(cxx_default "${cxx_enable_0x} ${cxx_exception}")
  message(STATUS "cxx_default: ${cxx_default}")

  set(cxx_strict "${cxx_default} ${cxx_strict_flags}")
endmacro()

#Define libraries. 
function(cxx_library_with_type name type cxx_flags libs)
  # type can be either STATIC or SHARED to denote a static or shared library.
  # ARGN refer to additional arguments after 'cxx_flags'.
  add_library(${name} ${type} ${ARGN})
  set_target_properties(${name}
    PROPERTIES
    COMPILE_FLAGS "${cxx_flags}")
  if (BUILD_SHARED_LIBS OR type STREQUAL "SHARED")
    set_target_properties(${name}
      PROPERTIES
      COMPILE_DEFINITIONS "PI_CREATE_SHARED_LIBRARY=1")
  endif()
  foreach (lib "${libs}")
    #message(STATUS "link lib: ${lib}")
    target_link_libraries(${name} ${lib})
  endforeach()
endfunction()

# Helper functions for creating build targets.

function(cxx_shared_library name cxx_flags libs)
  cxx_library_with_type(${name} SHARED "${cxx_flags}" "${libs}" ${ARGN})
endfunction()

function(cxx_library name cxx_flags libs)
  cxx_library_with_type(${name} "" "${cxx_flags}" "${libs}" ${ARGN})
endfunction()

#Define executable
function(cxx_executable_with_flags name cxx_flags libs)
  add_executable(${name} ${ARGN})
  if (cxx_flags)
    set_target_properties(${name}
      PROPERTIES
      COMPILE_FLAGS "${cxx_flags}")
  endif()
  if (BUILD_SHARED_LIBS)
    set_target_properties(${name}
      PROPERTIES
      COMPILE_DEFINITIONS "PI_LINKED_AS_SHARED_LIBRARY=1")
  endif()
  foreach (lib "${libs}")
    #message(STATUS "link lib: ${lib}")
    target_link_libraries(${name} ${lib})
  endforeach()
endfunction()

function(cxx_executable name dir libs)
  cxx_executable_with_flags(
    ${name} "${cxx_default}" "${libs}" "${dir}/${name}.cc" ${ARGN})
endfunction()

function(cxx_test_with_flags name cxx_flags libs)
  cxx_executable_with_flags(${name} "${cxx_flags}" "${libs}" ${ARGN})
  add_test(${name} ${name})
endfunction()

function(cxx_test name libs)
  cxx_test_with_flags("${name}" "${cxx_default}" "${libs}"
    "test/${name}.cc" ${ARGN})
endfunction()
