cmake_minimum_required(VERSION 3.5)
project(ips_mcc_stm32_IHM VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Busca o que precisamos no Linux da ENIB
find_package(Qt5 REQUIRED COMPONENTS Widgets SerialPort PrintSupport)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    qcustomplot.cpp
    qcustomplot.h
    mainwindow.ui
)

add_executable(ips_mcc_stm32_IHM ${PROJECT_SOURCES})

target_include_directories(ips_mcc_stm32_IHM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Linka as bibliotecas
target_link_libraries(ips_mcc_stm32_IHM PRIVATE
    Qt5::Widgets
    Qt5::SerialPort
    Qt5::PrintSupport
)

