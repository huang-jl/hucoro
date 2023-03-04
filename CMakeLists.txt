cmake_minimum_required(VERSION 3.23)
project(hucoro)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    # using regular Clang or AppleClang
    set(COROUTINE_OPTION "-fcoroutines-ts")
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(COROUTINE_OPTION "-fcoroutines")
else()
    message( FATAL_ERROR "Only support g++ and clang++ for now" )
endif ()


add_library(hucoro src/single_thread_scheduler.cpp src/spawn_task.cpp)
target_include_directories(hucoro PUBLIC ${PROJECT_SOURCE_DIR}/src/include)
target_compile_options(hucoro PUBLIC ${COROUTINE_OPTION})
set_target_properties(hucoro PROPERTIES LINKER_LANGUAGE CXX)


# TEST #
if (BUILD_TESTING)
    message("BUILD TEST")
    enable_testing()
    add_subdirectory(test)
endif ()

