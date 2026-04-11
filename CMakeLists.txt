cmake_minimum_required(VERSION 3.10)

set(CMAKE_C_COMPILER gcc)

project(QuakeDB3_0 C)

set(CMAKE_C_STANDARD 11)

include_directories(core/bufforing-stm/bufforing-stm)
include_directories(core/memory-mgmt/memory-mgmt)

# Shared library sources (no main)
set(CORE_SOURCES
        core/bufforing-stm/bufforing-stm/dataBuffor.c
        core/bufforing-stm/bufforing-stm/fsmMap.c
        core/memory-mgmt/memory-mgmt/all_var.c
        core/memory-mgmt/memory-mgmt/block8kb.c
        core/memory-mgmt/memory-mgmt/block_header.c
        core/memory-mgmt/memory-mgmt/data_null_bitmap.c
        core/memory-mgmt/memory-mgmt/file_manager_c.c
        core/memory-mgmt/memory-mgmt/table_header.c
        core/memory-mgmt/memory-mgmt/tuple.c
        core/memory-mgmt/memory-mgmt/tuple_header.c
        core/memory-mgmt/memory-mgmt/types_converter.c
        core/memory-mgmt/memory-mgmt/universal_block.c
)

# Main executable
add_executable(QuakeDB3_0
        ${CORE_SOURCES}
        core/main.c
)

# Find cmocka
find_library(CMOCKA_LIBRARY cmocka)

if(CMOCKA_LIBRARY)
    set(TEST_SOURCES
            core/memory-mgmt/tests/test_all_var.c
            core/memory-mgmt/tests/test_block8kb.c
            core/memory-mgmt/tests/test_block_header.c
            core/memory-mgmt/tests/test_table_header.c
            core/memory-mgmt/tests/test_tuple.c
            core/memory-mgmt/tests/test_types_converter.c
            core/memory-mgmt/tests/test_universal_block.c
    )

    foreach(TEST_FILE ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        add_executable(${TEST_NAME} ${CORE_SOURCES} ${TEST_FILE})
        target_link_libraries(${TEST_NAME} ${CMOCKA_LIBRARY})
    endforeach()
else()
    message(WARNING "cmocka not found — test targets skipped. Install with: sudo apt install libcmocka-dev")
endif()