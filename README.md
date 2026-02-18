# vMSC

A C++ implementation of the variant Minimum Spanning Cycle (vMSC) algorithm.

## Description

vMSC (variant Minimum Spanning Cycle) is a graph algorithm that finds a cycle in a graph with minimum total weight. It works by:
1. First constructing a Minimum Spanning Tree (MST) using Kruskal's algorithm
2. Then adding one additional edge to create a cycle with minimum total weight

## Building

To build the project, simply run:

```bash
make
```

This will compile the source files and create an executable named `vmsc`.

## Running

To run the program:

```bash
./vmsc
```

Or use the make target:

```bash
make run
```

## Usage

The implementation provides a `vMSC` class with the following interface:

```cpp
#include "vMSC.h"

// Create a graph with 5 vertices
vMSC graph(5);

// Add edges (vertex1, vertex2, weight)
graph.addEdge(0, 1, 2.0);
graph.addEdge(1, 2, 3.0);
graph.addEdge(2, 3, 4.0);

// Find the vMSC
graph.findvMSC();

// Print the result
graph.printVMSC();

// Get total weight
double weight = graph.getTotalWeight();
```

## Implementation Details

- The graph is represented using an adjacency list
- Uses a modified Kruskal's algorithm for MST construction
- Uses Union-Find data structure with path compression
- Time complexity: O(E log E) where E is the number of edges

## Cleaning

To remove compiled files:

```bash
make clean
```

## Requirements

- C++11 compatible compiler (g++, clang++)
- Make utility