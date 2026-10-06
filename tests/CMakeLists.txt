add_library(lsq_doctest INTERFACE)
target_include_directories(lsq_doctest SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party/doctest")

add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(backend)
