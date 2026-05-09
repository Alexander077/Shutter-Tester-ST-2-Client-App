cmake_minimum_required(VERSION 3.16)

project(ShutterTesterApp VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Company/publisher shown in file properties; change as needed
set(COMPANY_NAME "Alexander Litvinov")

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets SerialPort PrintSupport Charts)

qt_standard_project_setup(REQUIRES 6.8)

# Generate Windows VERSIONINFO resource from template
configure_file(${CMAKE_SOURCE_DIR}/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

qt_add_executable(appShutterTesterApp
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    SerialController.cpp
    SerialController.h
    FirmwareUpdateDialog.cpp
    FirmwareUpdateDialog.h
    LightSetupDialog.cpp
    LightSetupDialog.h
    ConnectionDialog.cpp
    ConnectionDialog.h
    resources.rc
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)

set_target_properties(appShutterTesterApp PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "Shutter Tester ST-2 App"
)

target_link_libraries(appShutterTesterApp
    PRIVATE Qt6::Widgets Qt6::SerialPort Qt6::PrintSupport Qt6::Charts
)

include(GNUInstallDirs)
install(TARGETS appShutterTesterApp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
