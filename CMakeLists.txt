cmake_minimum_required(VERSION 3.16)
project(Patcher)

set(CMAKE_CXX_STANDARD 17)
if (WIN32)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/mingw_64")
endif()

set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTOUIC ON)
#set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(Patcher
    main.cpp
)
if (WIN32)
    target_link_options(Patcher PRIVATE -mwindows)
endif()

target_link_libraries(Patcher
    Qt6::Widgets
)