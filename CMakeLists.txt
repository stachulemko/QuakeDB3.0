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
        core/bufforing-stm/bufforing-stm/transaction.h
        core/bufforing-stm/bufforing-stm/queryExecutor.c
        core/bufforing-stm/bufforing-stm/transaction.c
        core/bufforing-stm/bufforing-stm/setUpCaches.h
        core/bufforing-stm/bufforing-stm/setUpCaches.c
        core/bufforing-stm/bufforing-stm/mainLoop.h
        core/bufforing-stm/bufforing-stm/mainLoop.c
        core/memory-mgmt/memory-mgmt/timeFunction.h
        core/memory-mgmt/memory-mgmt/timeFunction.c
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
            core/bufforing-stm/tests/test_dataBuffor.c
            core/bufforing-stm/tests/test_fsmMap.c
    )

    add_executable(test_components
            ${CORE_SOURCES}
            core/bufforing-stm/components-tests/data_buffor_eviction_and_data_check_on_disk.c
            core/bufforing-stm/components-tests/sqlExecutorTest.c
            core/bufforing-stm/components-tests/sqlExecutor.c
    )
    target_link_libraries(test_components ${CMOCKA_LIBRARY})
    target_compile_definitions(test_components PRIVATE LOG_LEVEL=0)

    add_executable(test_tuple_add
            ${CORE_SOURCES}
            core/bufforing-stm/components-tests/tupleAddTest.c
    )
    target_link_libraries(test_tuple_add ${CMOCKA_LIBRARY})
    target_compile_definitions(test_tuple_add PRIVATE LOG_LEVEL=0)

    add_executable(test_stress
            ${CORE_SOURCES}
            core/bufforing-stm/components-tests/tupleAddingStressTest.c
    )
    target_link_libraries(test_stress ${CMOCKA_LIBRARY})
    target_compile_definitions(test_stress PRIVATE LOG_LEVEL=0)

    add_executable(test_btree_operations
            ${CORE_SOURCES}
            core/indexes/tests/test_btree_operations.c
    )
    target_compile_definitions(test_btree_operations PRIVATE LOG_LEVEL=0)

    add_executable(test_btree_update_indexes
            ${CORE_SOURCES}
            core/indexes/tests/test_btree_update_indexes.c
    )
    target_compile_definitions(test_btree_update_indexes PRIVATE LOG_LEVEL=0)

    add_executable(test_btree_block_count
            ${CORE_SOURCES}
            core/indexes/tests/test_btree_block_count.c
    )
    target_compile_definitions(test_btree_block_count PRIVATE LOG_LEVEL=0)

    add_executable(test_btree_get_blocks
            ${CORE_SOURCES}
            core/indexes/tests/test_btree_get_blocks.c
    )
    target_compile_definitions(test_btree_get_blocks PRIVATE LOG_LEVEL=0)

    add_executable(test_bq
            core/vacuum/vacuum/bq.c
            core/vacuum/tests/test_bq.c
    )
    target_compile_definitions(test_bq PRIVATE LOG_LEVEL=0)

    foreach(TEST_FILE ${TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        add_executable(${TEST_NAME} ${CORE_SOURCES} ${TEST_FILE}
                core/vacuum/vacuum/vacuum.h
                core/vacuum/vacuum/vacuum.c)
        target_link_libraries(${TEST_NAME} ${CMOCKA_LIBRARY})
        target_compile_definitions(${TEST_NAME} PRIVATE LOG_LEVEL=0)
    endforeach()

    add_executable(test_sqlExecutor
            ${CORE_SOURCES}
            core/bufforing-stm/tests/test_sqlExecutor.c
    )
    target_link_libraries(test_sqlExecutor ${CMOCKA_LIBRARY})
    target_compile_definitions(test_sqlExecutor PRIVATE LOG_LEVEL=0)

    add_executable(test_sqlExecutor_chain
            ${CORE_SOURCES}
            core/bufforing-stm/tests/test_sqlExecutor_chain.c
    )
    target_link_libraries(test_sqlExecutor_chain ${CMOCKA_LIBRARY})
    target_compile_definitions(test_sqlExecutor_chain PRIVATE LOG_LEVEL=0)

    add_executable(test_mvccBuffor
            core/bufforing-stm/tests/test_mvccBuffor.c
    )
    target_link_libraries(test_mvccBuffor ${CMOCKA_LIBRARY})
    target_compile_definitions(test_mvccBuffor PRIVATE LOG_LEVEL=0)

    add_executable(test_mvccBuffor_component
            core/bufforing-stm/components-tests/mvccBufforTest.c
    )
    target_link_libraries(test_mvccBuffor_component ${CMOCKA_LIBRARY})
    target_compile_definitions(test_mvccBuffor_component PRIVATE LOG_LEVEL=0)

    add_executable(test_mvcc_txn_overflow
            core/bufforing-stm/components-tests/mvccTransactionOverflowTest.c
    )
    target_link_libraries(test_mvcc_txn_overflow ${CMOCKA_LIBRARY})
    target_compile_definitions(test_mvcc_txn_overflow PRIVATE LOG_LEVEL=0)

else()
    message(WARNING "cmocka not found — test targets skipped. Install with: sudo apt install libcmocka-dev")
endif()