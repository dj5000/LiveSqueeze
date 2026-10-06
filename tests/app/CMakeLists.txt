# Qt targets found in app/ are not visible here (imported targets are directory scoped).
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Test)
set(CMAKE_AUTOMOC ON)

add_executable(lsq_app_tests
  main.cpp
  test_settings.cpp
  test_controller.cpp
  test_widgets.cpp
  test_a11y.cpp
  test_screenshots.cpp
)
target_link_libraries(lsq_app_tests PRIVATE lsq_app_lib lsq_doctest Qt6::Test)
lsq_set_warnings(lsq_app_tests)
target_compile_definitions(lsq_app_tests PRIVATE LSQ_SCREENSHOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/screenshots")

# The tests need no screen: Qt's "offscreen" platform draws into memory.
add_test(NAME lsq_app_tests COMMAND lsq_app_tests)
set_tests_properties(lsq_app_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
