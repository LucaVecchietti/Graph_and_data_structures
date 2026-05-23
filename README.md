# Graph_and_data_structures

In this repository different data structures and algorithms are implemented with the intent of optimizing graph data storage.

## Requirements

- [CMake](https://cmake.org/) >= 3.15
- [MSYS2](https://www.msys2.org/) with the `ucrt64` toolchain (g++ 16+, ninja)

Make sure the MSYS2 binaries are in your PATH:

```
C:\msys64\ucrt64\bin
```

## Build

```bash
# Configure
cmake -S . -B build -G "Ninja"

# Compile
cmake --build build
```

The executable is generated at `build/graph.exe`.

## Clean build

```bash
rm -rf build
cmake -S . -B build -G "Ninja"
cmake --build build
```

## Run

```bash
./build/graph.exe
```
