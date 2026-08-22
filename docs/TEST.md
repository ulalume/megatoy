## Running Tests (with `check` target)

All tests are wired to the `check` target, which builds the test binaries and runs CTest in one command.

### 1. Configure

```bash
cmake -S . -B build-release
```

### 2. Build and run all tests

```bash
cmake --build build-release --target check --parallel
```

This will:
- Build `megatoy_core` and all test executables.
- Run `ctest --output-on-failure` in `build-release`.

### Listing and running individual tests

```bash
ctest --test-dir build-release -N              # list
ctest --test-dir build-release -R patch_write  # run one
```
