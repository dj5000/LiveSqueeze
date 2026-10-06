# The Qt tray application. Needs Qt 6 (Widgets); skipped quietly when LSQ_BUILD_APP=AUTO and Qt is
# not installed.
find_package(Qt6 6.2 COMPONENTS Widgets Network Test QUIET)
if(NOT Qt6_FOUND)
  if(LSQ_BUILD_APP STREQUAL "ON")
    message(FATAL_ERROR "LSQ_BUILD_APP=ON but Qt 6 (Widgets) was not found")
  endif()
  message(STATUS "Qt 6 not found: the tray application is not built")
  return()
endif()

set(CMAKE_AUTOMOC ON)

add_library(lsq_app_lib STATIC
  Settings.cpp
  PresetStore.cpp
  Autostart.cpp
  AppController.cpp
  Icons.cpp
  Widgets.cpp
  MainWindow.cpp
  TrayController.cpp
  SingleInstance.cpp
)
target_include_directories(lsq_app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lsq_app_lib PUBLIC Qt6::Widgets Qt6::Network lsq::engine lsq::backends)
target_compile_features(lsq_app_lib PUBLIC cxx_std_20)
lsq_set_warnings(lsq_app_lib)

add_executable(livesqueeze WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(livesqueeze PRIVATE lsq_app_lib)
lsq_set_warnings(livesqueeze)

# ---- Windows: manifest (per-monitor DPI, UTF-8, no elevation) ------------------------------------
if(WIN32)
  target_sources(livesqueeze PRIVATE platform/windows/livesqueeze.manifest)
endif()

# ---- macOS: bundle with Info.plist (microphone text for the virtual cable) and ad-hoc signing ----
if(APPLE)
  set_target_properties(livesqueeze PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.livesqueeze.app"
    MACOSX_BUNDLE_BUNDLE_NAME "LiveSqueeze"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
  # Without any signature macOS refuses to run the bundle on Apple Silicon. An ad-hoc signature
  # (identity "-") is enough for running it on the machine that built it; distributing it to others
  # needs a Developer ID signature and notarisation (see docs/PLATFORMS.md).
  find_program(LSQ_CODESIGN codesign)
  if(LSQ_CODESIGN)
    add_custom_command(TARGET livesqueeze POST_BUILD
      COMMAND ${LSQ_CODESIGN} --force --sign - --options runtime
              --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/livesqueeze.entitlements"
              "$<TARGET_BUNDLE_DIR:livesqueeze>"
      COMMENT "Signing LiveSqueeze.app (ad hoc)")
  endif()
endif()
