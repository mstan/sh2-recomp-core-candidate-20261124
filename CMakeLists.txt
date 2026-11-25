cmake_minimum_required(VERSION 3.16)
project(sh2_recomp_core LANGUAGES C)

add_library(sh2_recomp_core STATIC src/decode.c src/step.c src/sh7604_bsc.c
    src/sh7604_system.c src/sh7604_cache.c src/sh7604_frt.c src/sh7604_dmac.c
    src/sh7604_divu.c)
add_library(sh2-recomp-core::core ALIAS sh2_recomp_core)
target_include_directories(sh2_recomp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(sh2_recomp_core PUBLIC c_std_11)

add_library(sh2_interpreter STATIC EXCLUDE_FROM_ALL src/interpreter.c)
add_library(sh2-recomp-core::interpreter ALIAS sh2_interpreter)
target_link_libraries(sh2_interpreter PUBLIC sh2-recomp-core::core)

add_library(sh2_catalog_emitter STATIC src/emit.c src/function_graph.c src/function_emit.c)
target_link_libraries(sh2_catalog_emitter PUBLIC sh2-recomp-core::core)
add_executable(sh2_aot tools/aot.c)
set_target_properties(sh2_aot PROPERTIES OUTPUT_NAME sh2-aot)
target_link_libraries(sh2_aot PRIVATE sh2_catalog_emitter)
add_executable(sh2_function_aot tools/function_aot.c)
set_target_properties(sh2_function_aot PROPERTIES OUTPUT_NAME sh2-function-aot)
target_link_libraries(sh2_function_aot PRIVATE sh2_catalog_emitter)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
