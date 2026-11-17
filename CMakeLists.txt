cmake_minimum_required(VERSION 3.20)
project(mm10_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# This target checks that the translated C compiles. It deliberately creates
# a static library and does not link or execute a game.
file(GLOB GENERATED_FUNCTIONS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/generated/funcs_*.c")

add_library(mm10_generated STATIC
    runtime_min.c
    runtime/src/generated/func_table.c
    ${GENERATED_FUNCTIONS}
)

target_include_directories(mm10_generated PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/generated"
)

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
