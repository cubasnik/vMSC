#include "vmsc.h"
#include <iostream>

VMSC::VMSC(const std::string& mscId) 
    : mscId(mscId), isRunning(false) {
    subscriberManager = std::make_unique<SubscriberManager>();
    callManager = std::make_unique<CallManager>();
    mobilityManager = std::make_unique<MobilityManager>();
}

bool VMSC::initialize() {
    std::cout << "Initializing vMSC: " << mscId << std::endl;
    return true;
}

bool VMSC::start() {
    if (isRunning) {
        std::cerr << "vMSC is already running" << std::endl;
        return false;
    }
    
    std::cout << "Starting vMSC: " << mscId << std::endl;
    isRunning = true;
    return true;
}

bool VMSC::stop() {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    std::cout << "Stopping vMSC: " << mscId << std::endl;
    isRunning = false;
    return true;
}

bool VMSC::registerSubscriber(const std::string& imsi, const std::string& msisdn) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    return subscriberManager->addSubscriber(imsi, msisdn);
}

bool VMSC::authenticateSubscriber(const std::string& imsi) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    return subscriberManager->authenticateSubscriber(imsi);
}

bool VMSC::updateSubscriberLocation(const std::string& imsi, const std::string& location) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    return subscriberManager->updateLocation(imsi, location);
}

std::string VMSC::initiateCall(const std::string& callerImsi, const std::string& calleeNumber) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return "";
    }
    
    if (!subscriberManager->isSubscriberActive(callerImsi)) {
        std::cerr << "Caller " << callerImsi << " is not authenticated" << std::endl;
        return "";
    }
    
    return callManager->setupCall(callerImsi, calleeNumber);
}

bool VMSC::answerCall(const std::string& callId) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    return callManager->connectCall(callId);
}

bool VMSC::endCall(const std::string& callId) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    return callManager->terminateCall(callId);
}

bool VMSC::performHandover(const std::string& imsi, 
                          const std::string& sourceLac, 
                          const std::string& targetLac) {
    if (!isRunning) {
        std::cerr << "vMSC is not running" << std::endl;
        return false;
    }
    
    if (!subscriberManager->isSubscriberActive(imsi)) {
        std::cerr << "Subscriber " << imsi << " is not authenticated" << std::endl;
        return false;
    }
    
    if (!mobilityManager->initiateHandover(imsi, sourceLac, targetLac)) {
        return false;
    }
    
    updateSubscriberLocation(imsi, targetLac);
    
    return mobilityManager->completeHandover(imsi);
}

void VMSC::printStatus() const {
    std::cout << "\n======================================" << std::endl;
    std::cout << "vMSC Status Report" << std::endl;
    std::cout << "MSC ID: " << mscId << std::endl;
    std::cout << "Status: " << (isRunning ? "RUNNING" : "STOPPED") << std::endl;
    std::cout << "======================================" << std::endl;
    
    subscriberManager->printSubscribers();
    callManager->printActiveCalls();
    mobilityManager->printHandovers();
    
    std::cout << "\n======================================\n" << std::endl;
}
