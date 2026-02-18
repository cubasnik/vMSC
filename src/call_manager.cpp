#include "call_manager.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>

CallManager::CallManager() : callCounter(0) {}

std::string CallManager::generateCallId() {
    std::ostringstream oss;
    oss << "CALL-" << std::setfill('0') << std::setw(8) << ++callCounter;
    return oss.str();
}

std::string CallManager::setupCall(const std::string& callerImsi, const std::string& calleeNumber) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string callId = generateCallId();
    auto call = std::make_shared<Call>(callId, callerImsi, calleeNumber);
    call->state = CallState::SETUP;
    call->setupTime = std::chrono::system_clock::now().time_since_epoch().count();
    
    activeCalls[callId] = call;
    
    std::cout << "Call setup initiated: CallID=" << callId 
              << ", Caller=" << callerImsi 
              << ", Callee=" << calleeNumber << std::endl;
    
    return callId;
}

bool CallManager::connectCall(const std::string& callId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = activeCalls.find(callId);
    if (it == activeCalls.end()) {
        std::cerr << "Call " << callId << " not found" << std::endl;
        return false;
    }
    
    auto& call = it->second;
    if (call->state != CallState::SETUP) {
        std::cerr << "Call " << callId << " is not in SETUP state" << std::endl;
        return false;
    }
    
    call->state = CallState::CONNECTED;
    call->connectTime = std::chrono::system_clock::now().time_since_epoch().count();
    
    std::cout << "Call connected: CallID=" << callId << std::endl;
    return true;
}

bool CallManager::terminateCall(const std::string& callId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = activeCalls.find(callId);
    if (it == activeCalls.end()) {
        std::cerr << "Call " << callId << " not found" << std::endl;
        return false;
    }
    
    auto& call = it->second;
    call->state = CallState::TERMINATED;
    
    std::cout << "Call terminated: CallID=" << callId << std::endl;
    
    activeCalls.erase(it);
    return true;
}

CallState CallManager::getCallState(const std::string& callId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = activeCalls.find(callId);
    if (it == activeCalls.end()) {
        return CallState::IDLE;
    }
    
    return it->second->state;
}

void CallManager::printActiveCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << "\n=== Active Calls ===" << std::endl;
    std::cout << std::setw(15) << "Call ID" 
              << std::setw(20) << "Caller IMSI" 
              << std::setw(15) << "Callee" 
              << std::setw(15) << "State" << std::endl;
    std::cout << std::string(65, '-') << std::endl;
    
    for (const auto& pair : activeCalls) {
        const auto& call = pair.second;
        std::string stateStr;
        
        switch (call->state) {
            case CallState::IDLE: stateStr = "IDLE"; break;
            case CallState::SETUP: stateStr = "SETUP"; break;
            case CallState::ALERTING: stateStr = "ALERTING"; break;
            case CallState::CONNECTED: stateStr = "CONNECTED"; break;
            case CallState::DISCONNECTING: stateStr = "DISCONNECTING"; break;
            case CallState::TERMINATED: stateStr = "TERMINATED"; break;
        }
        
        std::cout << std::setw(15) << call->callId
                  << std::setw(20) << call->callerImsi
                  << std::setw(15) << call->calleeNumber
                  << std::setw(15) << stateStr << std::endl;
    }
}
