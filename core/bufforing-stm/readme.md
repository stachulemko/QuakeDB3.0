## Commands to run bufforing-stm tests

### Build (from build/ directory)
```bash
cd build && cmake .. && make
```

### Unit tests
```bash
cd build && make test_dataBuffor && ./test_dataBuffor
cd build && make test_fsmMap && ./test_fsmMap
cd build && make test_QuaeryExecutor && ./test_QuaeryExecutor
cd build && make test_QuaeryExecutor_rc && ./test_QuaeryExecutor_rc
```

### SqlExecutor tests
```bash
cd build && make test_sqlExecutor && ./test_sqlExecutor
cd build && make test_sqlExecutor_chain && ./test_sqlExecutor_chain
```

### FullScan tests
```bash
cd build && make test_fullscan && ./test_fullscan
cd build && make test_fullScan_index && ./test_fullScan_index
```

### Component tests
```bash
cd build && make test_components && ./test_components
cd build && make test_tuple_add && ./test_tuple_add
cd build && make test_stress && ./test_stress
```