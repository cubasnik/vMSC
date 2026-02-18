#ifndef MOBILITY_MANAGER_H
#define MOBILITY_MANAGER_H

#include <string>
#include <unordered_map>
#include <mutex>

struct HandoverInfo {
    std::string imsi;
    std::string sourceLac;
    std::string targetLac;
    bool isCompleted;
    
    HandoverInfo(const std::string& imsi, const std::string& src, const std::string& tgt)
        : imsi(imsi), sourceLac(src), targetLac(tgt), isCompleted(false) {}
};

class MobilityManager {
private:
    std::unordered_map<std::string, HandoverInfo> activeHandovers;
    mutable std::mutex mutex_;

public:
    MobilityManager() = default;
    
    bool initiateHandover(const std::string& imsi, const std::string& sourceLac, 
                         const std::string& targetLac);
    bool completeHandover(const std::string& imsi);
    bool isHandoverInProgress(const std::string& imsi) const;
    void printHandovers() const;
};

#endif // MOBILITY_MANAGER_H
