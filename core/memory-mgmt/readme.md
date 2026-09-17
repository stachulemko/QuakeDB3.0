## Commands to run memory-mgmt tests

### Build (from build/ directory)
```bash
cd build && cmake .. && make
```

### Run all memory-mgmt tests
```bash
./build/test_all_var
./build/test_block8kb
./build/test_block_header
./build/test_table_header
./build/test_tuple
./build/test_types_converter
./build/test_universal_block
```

### Run single test (example)
```bash
cd build && make test_all_var && ./test_all_var
cd build && make test_block8kb && ./test_block8kb
cd build && make test_block_header && ./test_block_header
cd build && make test_table_header && ./test_table_header
cd build && make test_tuple && ./test_tuple
cd build && make test_types_converter && ./test_types_converter
cd build && make test_universal_block && ./test_universal_block
```