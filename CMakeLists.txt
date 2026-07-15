cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0087 NEW)

project(CommunicationAnalyzer VERSION 1.1.1 LANGUAGES CXX)

set(COMMUNICATION_ANALYZER_VERSION "${PROJECT_VERSION}" CACHE STRING
    "Three-part version used by the application and the MSI package"
)
set(COMMUNICATION_ANALYZER_UPDATE_REPOSITORY "leod86/CommunicationAnalyzer" CACHE STRING
    "GitHub repository used by the release updater"
)

if(NOT COMMUNICATION_ANALYZER_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "COMMUNICATION_ANALYZER_VERSION must contain exactly three numeric parts, for example 1.1.1"
    )
endif()

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

find_package(QT NAMES Qt6 REQUIRED COMPONENTS Network Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network Widgets SerialPort)

# Build only the Ela component library and skip its example application.
add_subdirectory(
    third_party/ElaWidgetTools/ElaWidgetTools
    ${CMAKE_BINARY_DIR}/ElaWidgetTools
    EXCLUDE_FROM_ALL
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
    src/update/UpdateManager.cpp
    src/update/UpdateManager.h
    src/widgets/TrafficTextEdit.cpp
    src/widgets/TrafficTextEdit.h
)

target_include_directories(CommunicationAnalyzer PRIVATE src)

target_link_libraries(CommunicationAnalyzer PRIVATE
    Ela::WidgetTools
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::SerialPort
)

target_compile_definitions(CommunicationAnalyzer PRIVATE
    COMMUNICATION_ANALYZER_UPDATE_REPOSITORY="${COMMUNICATION_ANALYZER_UPDATE_REPOSITORY}"
    COMMUNICATION_ANALYZER_VERSION="${COMMUNICATION_ANALYZER_VERSION}"
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

install(FILES cmake/InstallUpdate.ps1
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
                \"\${CMAKE_INSTALL_PREFIX}/CommunicationAnalyzer/$<TARGET_FILE_NAME:CommunicationAnalyzer>\"
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

set(CPACK_GENERATOR "WIX")
set(CPACK_PACKAGE_NAME "通讯协议分析仪")
set(CPACK_PACKAGE_VENDOR "leod86")
set(CPACK_PACKAGE_VERSION "${COMMUNICATION_ANALYZER_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "CommunicationAnalyzer")
set(CPACK_PACKAGE_FILE_NAME "CommunicationAnalyzer-x64")
set(CPACK_PACKAGE_EXECUTABLES "CommunicationAnalyzer" "通讯协议分析仪")
set(CPACK_CREATE_DESKTOP_LINKS "CommunicationAnalyzer")
set(CPACK_WIX_VERSION 3)
set(CPACK_WIX_UPGRADE_GUID "F6E4B037-4441-4097-8FAB-C50C9F8B1168")
set(CPACK_WIX_TEMPLATE "${CMAKE_SOURCE_DIR}/cmake/WIX.template.in")
set(CPACK_WIX_UI_REF "WixUI_InstallDir")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "通讯协议分析仪")
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT
    "https://github.com/${COMMUNICATION_ANALYZER_UPDATE_REPOSITORY}"
)
set(CPACK_WIX_PROPERTY_ARPHELPLINK
    "https://github.com/${COMMUNICATION_ANALYZER_UPDATE_REPOSITORY}/issues"
)

include(CPack)
