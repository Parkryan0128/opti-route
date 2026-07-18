cmake_minimum_required(VERSION 3.18)

project(optiroute_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)

# pybind11 is installed through pip. Add its CMake package location so this
# works consistently in the local virtualenv and the Docker image.
execute_process(
    COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
list(APPEND CMAKE_PREFIX_PATH "${PYBIND11_CMAKE_DIR}")
set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 CONFIG REQUIRED)

function(enable_strict_warnings target)
    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /WX)
    else()
        target_compile_options(
            "${target}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
        )
    endif()
endfunction()

add_library(optiroute_engine STATIC optimizer.cpp)
target_include_directories(
    optiroute_engine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_target_properties(
    optiroute_engine
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)
enable_strict_warnings(optiroute_engine)

pybind11_add_module(optiroute_cpp MODULE bindings.cpp)
target_link_libraries(optiroute_cpp PRIVATE optiroute_engine)
enable_strict_warnings(optiroute_cpp)

install(TARGETS optiroute_cpp LIBRARY DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_optimizer tests/test_optimizer.cpp)
    target_link_libraries(test_optimizer PRIVATE optiroute_engine)
    enable_strict_warnings(test_optimizer)
    add_test(NAME optimizer_cpp_tests COMMAND test_optimizer)

    add_test(
        NAME optimizer_python_binding_tests
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "PYTHONPATH=$<TARGET_FILE_DIR:optiroute_cpp>"
            "${Python_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bindings.py"
    )
endif()
