add_executable(lsq-run main.cpp)
target_link_libraries(lsq-run PRIVATE lsq::engine lsq::backends)
lsq_set_warnings(lsq-run)
install(TARGETS lsq-run RUNTIME DESTINATION bin)
