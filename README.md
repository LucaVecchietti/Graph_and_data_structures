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

#### PowerShell
```powershell
# Configure
cmake -S . -B build -G "Ninja"

# Compile
cmake --build build
```

#### Bash / MSYS2
```bash
cmake -S . -B build -G "Ninja"
cmake --build build
```

The executable is generated at `build/graph.exe`.

## Clean build

#### PowerShell
```powershell
Remove-Item -Recurse -Force build
cmake -S . -B build -G "Ninja"
cmake --build build
```

#### Bash / MSYS2
```bash
rm -rf build
cmake -S . -B build -G "Ninja"
cmake --build build
```

## Run

#### PowerShell
```powershell
.\build\graph.exe
```

#### Bash / MSYS2
```bash
./build/graph.exe
```

> The program ends with a `pause` — press any key to close the window.
> To keep the terminal open after execution, run the executable directly from PowerShell or a terminal (not by double-clicking).
