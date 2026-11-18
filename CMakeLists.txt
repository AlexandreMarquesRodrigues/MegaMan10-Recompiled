cmake_minimum_required(VERSION 3.20)
project(mm10_recomp C)

option(MM10_WATCH "Trace writes covering the DVD dispatcher callback slot" OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Generated translation is kept as a library for compile validation and linked
# into the first host executable below.
file(GLOB GENERATED_FUNCTIONS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/generated/funcs_*.c")

add_library(mm10_generated STATIC
    runtime_min.c
    runtime/src/generated/func_table.c
    ${GENERATED_FUNCTIONS}
)

set(FUNCTION_NAMES_C "${CMAKE_CURRENT_BINARY_DIR}/function_names.generated.c")
add_custom_command(
    OUTPUT "${FUNCTION_NAMES_C}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_function_names.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/games/megaman10/func_list.json" "${FUNCTION_NAMES_C}"
    DEPENDS tools/gen_function_names.py games/megaman10/func_list.json
    VERBATIM
)

add_executable(mm10 runtime/runtime.c runtime/hle_exi.c runtime/hle_audio.c "${FUNCTION_NAMES_C}")
target_include_directories(mm10 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/generated"
)
target_link_libraries(mm10 PRIVATE mm10_generated)
if(NOT MSVC)
    target_link_libraries(mm10 PRIVATE m)
endif()

target_include_directories(mm10_generated PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/generated"
)
target_compile_definitions(mm10_generated PRIVATE PPC_TRACE_ALL=1)
if(MM10_WATCH)
    target_compile_definitions(mm10 PRIVATE MM10_WATCH=1)
    target_compile_definitions(mm10_generated PRIVATE MM10_WATCH=1)
endif()

if(MSVC)
    target_compile_options(mm10_generated PRIVATE /W0)
else()
    target_compile_options(mm10_generated PRIVATE
        -Wno-parentheses
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-unused-label
        -Wno-unused-function
        -Wno-implicit-fallthrough
        -Wno-overflow
        -Wno-pointer-to-int-cast
        -Wno-int-to-pointer-cast
        -Wno-shift-count-overflow
        -Wno-sign-compare
        -Wno-missing-braces
        -Wno-missing-field-initializers
        -Wno-type-limits
    )
endif()
