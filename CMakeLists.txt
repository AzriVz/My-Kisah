cmake_minimum_required(VERSION 3.20)

project(desktop_decompiler
    VERSION 0.1.0
    DESCRIPTION "A desktop decompiler for ELF x86-64 binaries"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
find_package(Capstone REQUIRED)

add_library(decompiler_gui STATIC)

target_sources(decompiler_gui
    PRIVATE
        src/MainWindow.cpp
    PUBLIC
        include/MainWindow.hpp
)

target_include_directories(decompiler_gui
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(decompiler_gui
    PUBLIC
        Qt6::Widgets
)

add_executable(decompiler
    src/main.cpp
)

target_link_libraries(decompiler
    PRIVATE
        decompiler_gui
        Capstone::Capstone
)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

enable_project_warnings(decompiler_gui)
enable_project_warnings(decompiler)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
