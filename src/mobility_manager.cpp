#include "mobility_manager.h"
#include <iostream>
#include <iomanip>

bool MobilityManager::initiateHandover(const std::string& imsi, 
                                      const std::string& sourceLac, 
                                      const std::string& targetLac) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (activeHandovers.find(imsi) != activeHandovers.end()) {
        std::cerr << "Handover already in progress for IMSI " << imsi << std::endl;
        return false;
    }
    
    activeHandovers.emplace(imsi, HandoverInfo(imsi, sourceLac, targetLac));
    
    std::cout << "Handover initiated: IMSI=" << imsi 
              << ", Source LAC=" << sourceLac 
              << ", Target LAC=" << targetLac << std::endl;
    
    return true;
}

bool MobilityManager::completeHandover(const std::string& imsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = activeHandovers.find(imsi);
    if (it == activeHandovers.end()) {
        std::cerr << "No active handover found for IMSI " << imsi << std::endl;
        return false;
    }
    
    std::cout << "Handover completed for IMSI=" << imsi << std::endl;
    
    activeHandovers.erase(it);
    return true;
}

bool MobilityManager::isHandoverInProgress(const std::string& imsi) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return activeHandovers.find(imsi) != activeHandovers.end();
}

void MobilityManager::printHandovers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "\n=== Active Handovers ===" << std::endl;
    std::cout << std::setw(20) << "IMSI" 
              << std::setw(15) << "Source LAC" 
              << std::setw(15) << "Target LAC" 
              << std::setw(12) << "Completed" << std::endl;
    std::cout << std::string(62, '-') << std::endl;
    
    for (const auto& pair : activeHandovers) {
        const auto& ho = pair.second;
        std::cout << std::setw(20) << ho.imsi
                  << std::setw(15) << ho.sourceLac
                  << std::setw(15) << ho.targetLac
                  << std::setw(12) << (ho.isCompleted ? "Yes" : "No") << std::endl;
    }
}
