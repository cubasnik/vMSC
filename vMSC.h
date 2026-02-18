#ifndef VMSC_H
#define VMSC_H

#include <vector>
#include <utility>
#include <limits>

class vMSC {
public:
    // Constructor
    vMSC(int numVertices);
    
    // Add edge to the graph
    void addEdge(int u, int v, double weight);
    
    // Find variant Minimum Spanning Cycle
    std::vector<std::pair<int, int>> findvMSC();
    
    // Get the total weight of the vMSC
    double getTotalWeight() const;
    
    // Print the vMSC
    void printVMSC() const;
    
private:
    int n; // Number of vertices
    std::vector<std::vector<std::pair<int, double>>> adjList; // Adjacency list
    std::vector<std::pair<int, int>> mscEdges; // Edges in the vMSC
    double totalWeight;
    
    // Helper functions
    void dfs(int v, std::vector<bool>& visited);
    bool isConnected();
};

#endif // VMSC_H
