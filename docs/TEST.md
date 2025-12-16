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

### Included tests

- `random_utils_test` — core utility checks.
- `patch_registry_test` — verifies export formats (DefleMask/MML) are registered.
- `patch_io_roundtrip_test` — roundtrips gin → ginpkg, gin → dmp → gin, gin → mml → gin.
- `subsystem_tests` — lightweight subsystem checks (patch session snapshot, note allocation).
