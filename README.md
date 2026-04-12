# xushi

## Building

Requires CMake 3.20+ and a C++17 compiler.

```
make            # configure and build (Release)
make test       # build and run tests
make run        # build and run with the default scenario
make clean      # remove the build directory
```

Override defaults with variables:

```
make BUILD_TYPE=Debug          # debug build
make SCENARIO=scenarios/benchmark_dense.json run
```
