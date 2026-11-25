add_executable(sh2_decode_test decode_test.c)
target_link_libraries(sh2_decode_test PRIVATE sh2-recomp-core::core)
if(MSVC)
    target_compile_options(sh2_decode_test PRIVATE /W4 /WX)
else()
    target_compile_options(sh2_decode_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME sh2_decode COMMAND sh2_decode_test)


add_executable(sh2_generate_step_fixture generate_step.c)
target_link_libraries(sh2_generate_step_fixture PRIVATE sh2_catalog_emitter)
set(fixture_catalog "${CMAKE_CURRENT_BINARY_DIR}/fixture_catalog.c")
add_custom_command(OUTPUT "${fixture_catalog}"
    COMMAND sh2_generate_step_fixture "${fixture_catalog}"
    DEPENDS sh2_generate_step_fixture
    VERBATIM)
add_executable(sh2_step_test EXCLUDE_FROM_ALL step_test.c "${fixture_catalog}")
target_link_libraries(sh2_step_test PRIVATE sh2-recomp-core::interpreter)
option(SH2_BUILD_REFERENCE_TESTS "Build explicit reference interpreter comparison" OFF)
if(SH2_BUILD_REFERENCE_TESTS)
  set_target_properties(sh2_step_test sh2_interpreter PROPERTIES EXCLUDE_FROM_ALL FALSE)
  add_test(NAME sh2_step COMMAND sh2_step_test)
endif()

add_executable(sh7604_bsc_test sh7604_bsc_test.c)
target_link_libraries(sh7604_bsc_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh7604_bsc COMMAND sh7604_bsc_test)

add_executable(sh7604_system_test sh7604_system_test.c)
target_link_libraries(sh7604_system_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh7604_system COMMAND sh7604_system_test)

add_executable(sh7604_cache_test sh7604_cache_test.c)
target_link_libraries(sh7604_cache_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh7604_cache COMMAND sh7604_cache_test)

add_executable(sh7604_frt_test sh7604_frt_test.c)
target_link_libraries(sh7604_frt_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh7604_frt COMMAND sh7604_frt_test)
add_executable(sh7604_dmac_test sh7604_dmac_test.c)
target_link_libraries(sh7604_dmac_test PRIVATE sh2_recomp_core)
add_test(NAME sh7604_dmac COMMAND sh7604_dmac_test)

add_executable(sh7604_divu_test sh7604_divu_test.c)
target_link_libraries(sh7604_divu_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh7604_divu COMMAND sh7604_divu_test)

add_executable(sh2_native_step_test native_step_test.c "${fixture_catalog}")
target_link_libraries(sh2_native_step_test PRIVATE sh2-recomp-core::core)
add_test(NAME sh2_native_step COMMAND sh2_native_step_test)

add_executable(sh2_generate_function generate_function.c)
target_link_libraries(sh2_generate_function PRIVATE sh2_catalog_emitter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixture_function.c"
 COMMAND sh2_generate_function "${CMAKE_CURRENT_BINARY_DIR}/fixture_function.c"
 DEPENDS sh2_generate_function VERBATIM)
add_executable(sh2_function_execution function_execution.c "${CMAKE_CURRENT_BINARY_DIR}/fixture_function.c")
target_link_libraries(sh2_function_execution PRIVATE sh2-recomp-core::core)
add_test(NAME sh2_function_execution COMMAND sh2_function_execution)
