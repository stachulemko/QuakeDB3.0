## Commands to run indexes tests

### Build (from build/ directory)
```bash
cd build && cmake .. && make
```

### Btree tests
```bash
cd build && make test_btree_operations && ./test_btree_operations
cd build && make test_btree_update_indexes && ./test_btree_update_indexes
cd build && make test_btree_block_count && ./test_btree_block_count
cd build && make test_btree_get_blocks && ./test_btree_get_blocks
```