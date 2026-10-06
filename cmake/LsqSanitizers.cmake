# Global sanitizer switch: -DLSQ_SANITIZE="address;undefined" or -DLSQ_SANITIZE=thread
if(LSQ_SANITIZE AND NOT MSVC)
  string(REPLACE ";" "," _lsq_san "${LSQ_SANITIZE}")
  add_compile_options(-fsanitize=${_lsq_san} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=${_lsq_san})
  message(STATUS "LiveSqueeze: sanitizers enabled: ${_lsq_san}")
endif()
