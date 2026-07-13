cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0087 NEW)

project(CommunicationAnalyzer VERSION 0.1.0 LANGUAGES CXX)

option(COMMUNICATION_ANALYZER_RUN_AFTER_BUILD
    "Launch CommunicationAnalyzer automatically after a successful build"
    ON
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/Install" CACHE PATH "Install path" FORCE)
endif()

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)

# Build only the Ela component library and skip its example application.
add_subdirectory(
    third_party/ElaWidgetTools/ElaWidgetTools
    ${CMAKE_BINARY_DIR}/ElaWidgetTools
)

qt_add_executable(CommunicationAnalyzer
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/communication/SerialController.cpp
    src/communication/SerialController.h
    src/pages/MonitorPage.cpp
    src/pages/MonitorPage.h
    src/pages/SettingPage.cpp
    src/pages/SettingPage.h
    src/widgets/TrafficTextEdit.cpp
    src/widgets/TrafficTextEdit.h
)

target_include_directories(CommunicationAnalyzer PRIVATE src)

target_link_libraries(CommunicationAnalyzer PRIVATE
    Ela::WidgetTools
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::SerialPort
)

set_target_properties(CommunicationAnalyzer PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS CommunicationAnalyzer
    RUNTIME DESTINATION CommunicationAnalyzer
)

install(FILES $<TARGET_FILE:ElaWidgetTools>
    DESTINATION CommunicationAnalyzer
)

get_target_property(QMAKE_EXECUTABLE Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}" REQUIRED)

if(WIN32)
    install(CODE "
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --release
                --compiler-runtime
                --no-system-d3d-compiler
                --no-opengl-sw
                \"${CMAKE_INSTALL_PREFIX}/CommunicationAnalyzer/$<TARGET_FILE_NAME:CommunicationAnalyzer>\"
            RESULT_VARIABLE DEPLOY_RESULT
        )
        if(NOT DEPLOY_RESULT EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed with code: \${DEPLOY_RESULT}\")
        endif()
    ")
endif()

if(WIN32 AND COMMUNICATION_ANALYZER_RUN_AFTER_BUILD)
    add_custom_command(TARGET CommunicationAnalyzer POST_BUILD
        COMMAND powershell.exe
            -NoProfile
            -NonInteractive
            -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/cmake/RunAfterBuild.ps1"
            -Executable "$<TARGET_FILE:CommunicationAnalyzer>"
            -QtBinDir "${QT_BIN_DIR}"
            -ElaBinDir "$<TARGET_FILE_DIR:ElaWidgetTools>"
        COMMENT "Launching CommunicationAnalyzer after successful build"
        VERBATIM
    )
endif()
