#include "vMSC.h"
#include <iostream>

int main() {
    // Example usage of vMSC
    std::cout << "=== vMSC (variant Minimum Spanning Cycle) Example ===" << std::endl;
    std::cout << std::endl;
    
    // Create a graph with 5 vertices
    vMSC graph(5);
    
    // Add edges (vertex1, vertex2, weight)
    graph.addEdge(0, 1, 2.0);
    graph.addEdge(0, 3, 6.0);
    graph.addEdge(1, 2, 3.0);
    graph.addEdge(1, 3, 8.0);
    graph.addEdge(1, 4, 5.0);
    graph.addEdge(2, 4, 7.0);
    graph.addEdge(3, 4, 9.0);
    
    std::cout << "Finding vMSC..." << std::endl;
    graph.findvMSC();
    std::cout << std::endl;
    
    graph.printVMSC();
    std::cout << std::endl;
    
    // Another example with different graph
    std::cout << "=== Second Example ===" << std::endl;
    std::cout << std::endl;
    
    vMSC graph2(4);
    graph2.addEdge(0, 1, 10.0);
    graph2.addEdge(0, 2, 6.0);
    graph2.addEdge(0, 3, 5.0);
    graph2.addEdge(1, 3, 15.0);
    graph2.addEdge(2, 3, 4.0);
    
    std::cout << "Finding vMSC..." << std::endl;
    graph2.findvMSC();
    std::cout << std::endl;
    
    graph2.printVMSC();
    
    return 0;
}
