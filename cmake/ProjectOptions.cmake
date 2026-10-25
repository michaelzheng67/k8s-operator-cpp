# Per-target settings for our own code only. Third-party deps from vcpkg are
# built without these flags so their warnings don't break our build.

set(K8SOP_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wold-style-cast
  -Wnon-virtual-dtor
  -Woverloaded-virtual
  -Wnull-dereference
  -Wimplicit-fallthrough
  -Wcast-align
  -Wdouble-promotion
)

if(K8SOP_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

function(k8sop_configure_target target)
  target_compile_options(${target} PRIVATE ${K8SOP_WARNINGS})
  if(K8SOP_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()

  if(K8SOP_SANITIZER)
    target_compile_options(${target} PRIVATE
      -fsanitize=${K8SOP_SANITIZER}
      -fno-omit-frame-pointer
      -fno-sanitize-recover=all
    )
    target_link_options(${target} PRIVATE -fsanitize=${K8SOP_SANITIZER})
  endif()

  if(K8SOP_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  endif()
endfunction()
