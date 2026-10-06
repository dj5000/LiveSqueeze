# lsq_set_warnings(<target>): project-wide warning policy.
function(lsq_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf8 /permissive- /Zc:__cplusplus /wd4100 /wd4324)
    target_compile_definitions(${target} PRIVATE NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
    if(LSQ_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual
      -Wdouble-promotion -Wformat=2)
    if(LSQ_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
