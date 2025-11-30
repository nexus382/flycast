# Tests

## Running the unit tests

The unit tests use CMake and Google Test. To build and execute them:

1. Initialize and fetch submodules so the bundled dependencies (breakpad, libusb, SDL, libchdr, libjuice, Vulkan headers, googletest, etc.) are available.
2. Configure a build with testing enabled and generate the binaries.
3. Run the test suite with `ctest` or execute an individual test binary directly.

Example commands:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R HostfsPathTest -V
```

This runs only the hostfs path tests, including the save-path consistency check for the region-tagged game.
