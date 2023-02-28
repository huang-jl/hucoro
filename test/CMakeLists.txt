add_subdirectory(Catch2)

include(CTEST)

add_library(test-lib STATIC counter.cpp counter.h)
target_link_libraries(test-lib INTERFACE hucoro)

file(GLOB test_files ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
# counter is use to test, itself is not a test
list(FILTER test_files EXCLUDE REGEX .*counter.cpp)

foreach(test_file ${test_files})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE test-lib Catch2::Catch2WithMain)
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
    # on macos, we use leaks for memory leak.
    # If there is not memory leak but test fail, the return value of leaks is still 0.
    # So have to run it in another round to check memory leaks.
    add_test(NAME ${test_name}_memory_leak COMMAND leaks --atExit -- $<TARGET_FILE:${test_name}>)
endforeach()
