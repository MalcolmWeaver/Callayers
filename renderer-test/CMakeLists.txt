cmake_minimum_required(VERSION 3.24)
project(CallayersRendererTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 CALLAYERS_RENDERER_TEST)
add_compile_options(/W4 /EHsc /utf-8)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(ULTRALIGHT_ROOT "${REPO}/.deps/ultralight" CACHE PATH "Ultralight SDK")
add_executable(CallayersUltralightTest WIN32 main.cpp)
target_include_directories(CallayersUltralightTest PRIVATE "${ULTRALIGHT_ROOT}/include" "${REPO}/native")
target_link_directories(CallayersUltralightTest PRIVATE "${ULTRALIGHT_ROOT}/lib")
target_link_libraries(CallayersUltralightTest PRIVATE Ultralight UltralightCore WebCore AppCore windowsapp ole32 user32 gdi32)
add_custom_command(TARGET CallayersUltralightTest POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${REPO}/web" "$<TARGET_FILE_DIR:CallayersUltralightTest>/web")
add_custom_command(TARGET CallayersUltralightTest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ULTRALIGHT_ROOT}/bin" "$<TARGET_FILE_DIR:CallayersUltralightTest>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ULTRALIGHT_ROOT}/resources" "$<TARGET_FILE_DIR:CallayersUltralightTest>/resources"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${ULTRALIGHT_ROOT}/license" "$<TARGET_FILE_DIR:CallayersUltralightTest>/ultralight-license")
