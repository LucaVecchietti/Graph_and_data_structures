cmake_minimum_required(VERSION 3.15)
project(pointer_graphs)

set(CMAKE_CXX_STANDARD 17)

# ── Libreria core ─────────────────────────────────────────────────────
add_library(graph_core
    graph_core/graph.cpp
    graph_core/io/graph_io.cpp
    graph_core/io/io_utils.cpp
    graph_core/odt/edge_odt.cpp
    graph_core/odt/node_odt.cpp
    graph_core/struct/type_registry.cpp
)

target_include_directories(graph_core PUBLIC graph_core)

# ── Eseguibile ────────────────────────────────────────────────────────
add_executable(graph main.cpp)
target_link_libraries(graph PRIVATE graph_core)
