#include "vMSC.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <tuple>
#include <set>

vMSC::vMSC(int numVertices) : n(numVertices), totalWeight(0.0) {
    adjList.resize(n);
}

void vMSC::addEdge(int u, int v, double weight) {
    if (u >= 0 && u < n && v >= 0 && v < n) {
        adjList[u].push_back({v, weight});
        adjList[v].push_back({u, weight});
    }
}

void vMSC::dfs(int v, std::vector<bool>& visited) {
    visited[v] = true;
    for (const auto& edge : adjList[v]) {
        int neighbor = edge.first;
        if (!visited[neighbor]) {
            dfs(neighbor, visited);
        }
    }
}

bool vMSC::isConnected() {
    std::vector<bool> visited(n, false);
    dfs(0, visited);
    for (bool v : visited) {
        if (!v) return false;
    }
    return true;
}

std::vector<std::pair<int, int>> vMSC::findvMSC() {
    mscEdges.clear();
    totalWeight = 0.0;
    
    if (!isConnected()) {
        std::cerr << "Graph is not connected!" << std::endl;
        return mscEdges;
    }
    
    // Create edge list with weights
    std::vector<std::tuple<double, int, int>> edges;
    for (int u = 0; u < n; ++u) {
        for (const auto& edge : adjList[u]) {
            int v = edge.first;
            double weight = edge.second;
            if (u < v) { // Avoid duplicate edges
                edges.push_back(std::make_tuple(weight, u, v));
            }
        }
    }
    
    // Sort edges by weight
    std::sort(edges.begin(), edges.end());
    
    // Use Kruskal-like approach to build MST first
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    
    auto find = [&](int x) {
        int root = x;
        while (parent[root] != root) {
            root = parent[root];
        }
        // Path compression
        while (parent[x] != root) {
            int next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    };
    
    auto unite = [&](int x, int y) {
        int rootX = find(x);
        int rootY = find(y);
        if (rootX != rootY) {
            parent[rootX] = rootY;
            return true;
        }
        return false;
    };
    
    // Build MST
    std::vector<std::pair<int, int>> mstEdges;
    std::set<std::pair<int, int>> mstEdgeSet; // For O(log E) lookup
    double mstWeight = 0.0;
    
    for (const auto& edge : edges) {
        double weight = std::get<0>(edge);
        int u = std::get<1>(edge);
        int v = std::get<2>(edge);
        
        if (unite(u, v)) {
            mstEdges.push_back({u, v});
            // Store both orderings for easy lookup
            mstEdgeSet.insert({std::min(u, v), std::max(u, v)});
            mstWeight += weight;
            if (mstEdges.size() == static_cast<size_t>(n - 1)) break;
        }
    }
    
    // For vMSC, we need to add one more edge to create a cycle
    // Find the minimum weight edge that creates a cycle
    // Since edges are sorted, the first non-MST edge has minimum weight
    bool foundCycleEdge = false;
    double minCycleEdgeWeight = 0.0;
    std::pair<int, int> minCycleEdge = {-1, -1};
    
    for (const auto& edge : edges) {
        double weight = std::get<0>(edge);
        int u = std::get<1>(edge);
        int v = std::get<2>(edge);
        
        // Check if this edge is not in MST using set for O(log E) lookup
        if (mstEdgeSet.find({std::min(u, v), std::max(u, v)}) == mstEdgeSet.end()) {
            minCycleEdgeWeight = weight;
            minCycleEdge = {u, v};
            foundCycleEdge = true;
            break; // First non-MST edge is minimum due to sorted order
        }
    }
    
    // Construct vMSC: MST + one edge to form cycle
    mscEdges = mstEdges;
    if (foundCycleEdge) {
        mscEdges.push_back(minCycleEdge);
        totalWeight = mstWeight + minCycleEdgeWeight;
    } else {
        totalWeight = mstWeight;
    }
    
    return mscEdges;
}

double vMSC::getTotalWeight() const {
    return totalWeight;
}

void vMSC::printVMSC() const {
    std::cout << "variant Minimum Spanning Cycle (vMSC):" << std::endl;
    std::cout << "Total weight: " << totalWeight << std::endl;
    std::cout << "Edges:" << std::endl;
    for (const auto& edge : mscEdges) {
        std::cout << edge.first << " -- " << edge.second << std::endl;
    }
}
