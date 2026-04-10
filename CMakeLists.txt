cmake_minimum_required(VERSION 4.2)
project(QuakeDB3_0 C)

set(CMAKE_C_STANDARD 11)

include_directories(core/bufforing-stm/bufforing-stm)
include_directories(core/memory-mgmt/memory-mgmt)

add_executable(QuakeDB3_0
        core/bufforing-stm/bufforing-stm/dataBuffor.c
        core/bufforing-stm/bufforing-stm/dataBuffor.h
        core/bufforing-stm/bufforing-stm/fsmMap.c
        core/bufforing-stm/bufforing-stm/fsmMap.h
        core/bufforing-stm/bufforing-stm/uthash.h
        core/memory-mgmt/memory-mgmt/all_var.c
        core/memory-mgmt/memory-mgmt/all_var.h
        core/memory-mgmt/memory-mgmt/block8kb.c
        core/memory-mgmt/memory-mgmt/block8kb.h
        core/memory-mgmt/memory-mgmt/block_header.c
        core/memory-mgmt/memory-mgmt/block_header.h
        core/memory-mgmt/memory-mgmt/config.h
        core/memory-mgmt/memory-mgmt/data_null_bitmap.c
        core/memory-mgmt/memory-mgmt/data_null_bitmap.h
        core/memory-mgmt/memory-mgmt/file_manager_c.c
        core/memory-mgmt/memory-mgmt/file_manager_c.h
        core/memory-mgmt/memory-mgmt/log.h
        core/memory-mgmt/memory-mgmt/table_header.c
        core/memory-mgmt/memory-mgmt/table_header.h
        core/memory-mgmt/memory-mgmt/tuple.c
        core/memory-mgmt/memory-mgmt/tuple.h
        core/memory-mgmt/memory-mgmt/tuple_header.c
        core/memory-mgmt/memory-mgmt/tuple_header.h
        core/memory-mgmt/memory-mgmt/type_ids.h
        core/memory-mgmt/memory-mgmt/types_converter.c
        core/memory-mgmt/memory-mgmt/types_converter.h
        core/memory-mgmt/memory-mgmt/universal_block.c
        core/memory-mgmt/memory-mgmt/universal_block.h
        core/memory-mgmt/tests/test_all_var.c
        core/memory-mgmt/tests/test_block8kb.c
        core/memory-mgmt/tests/test_block_header.c
        core/memory-mgmt/tests/test_table_header.c
        core/memory-mgmt/tests/test_tuple.c
        core/memory-mgmt/tests/test_types_converter.c
        core/memory-mgmt/tests/test_universal_block.c
        core/main.c)
