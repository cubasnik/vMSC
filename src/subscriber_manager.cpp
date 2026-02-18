#include "subscriber_manager.h"
#include <iostream>
#include <iomanip>

bool SubscriberManager::addSubscriber(const std::string& imsi, const std::string& msisdn) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (subscribers.find(imsi) != subscribers.end()) {
        std::cerr << "Subscriber with IMSI " << imsi << " already exists" << std::endl;
        return false;
    }
    
    subscribers.emplace(imsi, Subscriber(imsi, msisdn));
    std::cout << "Subscriber registered: IMSI=" << imsi << ", MSISDN=" << msisdn << std::endl;
    return true;
}

bool SubscriberManager::authenticateSubscriber(const std::string& imsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers.find(imsi);
    if (it == subscribers.end()) {
        std::cerr << "Subscriber with IMSI " << imsi << " not found" << std::endl;
        return false;
    }
    
    it->second.isActive = true;
    std::cout << "Subscriber authenticated: IMSI=" << imsi << std::endl;
    return true;
}

bool SubscriberManager::updateLocation(const std::string& imsi, const std::string& location) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers.find(imsi);
    if (it == subscribers.end()) {
        std::cerr << "Subscriber with IMSI " << imsi << " not found" << std::endl;
        return false;
    }
    
    it->second.location = location;
    std::cout << "Location updated for IMSI=" << imsi << " to " << location << std::endl;
    return true;
}

Subscriber* SubscriberManager::getSubscriber(const std::string& imsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers.find(imsi);
    if (it == subscribers.end()) {
        return nullptr;
    }
    
    return &it->second;
}

bool SubscriberManager::isSubscriberActive(const std::string& imsi) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscribers.find(imsi);
    if (it == subscribers.end()) {
        return false;
    }
    
    return it->second.isActive;
}

void SubscriberManager::printSubscribers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "\n=== Registered Subscribers ===" << std::endl;
    std::cout << std::setw(20) << "IMSI" 
              << std::setw(15) << "MSISDN" 
              << std::setw(15) << "Location" 
              << std::setw(10) << "Active" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    
    for (const auto& pair : subscribers) {
        const auto& sub = pair.second;
        std::cout << std::setw(20) << sub.imsi
                  << std::setw(15) << sub.msisdn
                  << std::setw(15) << (sub.location.empty() ? "N/A" : sub.location)
                  << std::setw(10) << (sub.isActive ? "Yes" : "No") << std::endl;
    }
}
