cmake_minimum_required(VERSION 3.24)
project(CallayersLinux VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(WIN32)
  message(FATAL_ERROR "The Linux app is built on Linux. The Windows app uses the repository root.")
endif()
find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus)
add_executable(callayers
  src/main.cpp
  src/auth_callback.cpp
  src/capture.cpp
  src/output.cpp
  src/compose.cpp
  src/store.cpp
  src/secret.cpp
  src/auth.cpp
  src/preview.cpp
  src/window.cpp)
target_include_directories(callayers PRIVATE src)
target_link_libraries(callayers PRIVATE Qt6::Widgets Qt6::Network Qt6::DBus)
target_compile_definitions(callayers PRIVATE "CALLAYERS_VERSION=\"${PROJECT_VERSION}\"")
install(TARGETS callayers RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../spec-v2/assets/callayers-mark.svg" DESTINATION share/icons/hicolor/scalable/apps RENAME callayers.svg)
install(FILES packaging/callayers.desktop DESTINATION share/applications)
include(CTest)
if(BUILD_TESTING)
  add_executable(linux_checks tests/checks.cpp src/auth_callback.cpp)
  target_include_directories(linux_checks PRIVATE src)
  add_test(NAME linux_video_and_callback COMMAND linux_checks)
  add_test(NAME linux_install_check COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/packaging/install.sh" --check)
endif()
