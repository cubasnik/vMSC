#ifndef VMSC_H
#define VMSC_H

#include <memory>
#include <string>
#include "subscriber_manager.h"
#include "call_manager.h"
#include "mobility_manager.h"

class VMSC {
private:
    std::unique_ptr<SubscriberManager> subscriberManager;
    std::unique_ptr<CallManager> callManager;
    std::unique_ptr<MobilityManager> mobilityManager;
    std::string mscId;
    bool isRunning;

public:
    explicit VMSC(const std::string& mscId);
    ~VMSC() = default;
    
    bool initialize();
    bool start();
    bool stop();
    
    // Subscriber operations
    bool registerSubscriber(const std::string& imsi, const std::string& msisdn);
    bool authenticateSubscriber(const std::string& imsi);
    bool updateSubscriberLocation(const std::string& imsi, const std::string& location);
    
    // Call operations
    std::string initiateCall(const std::string& callerImsi, const std::string& calleeNumber);
    bool answerCall(const std::string& callId);
    bool endCall(const std::string& callId);
    
    // Mobility operations
    bool performHandover(const std::string& imsi, const std::string& sourceLac, 
                        const std::string& targetLac);
    
    // Status
    void printStatus() const;
    bool isActive() const { return isRunning; }
    std::string getMscId() const { return mscId; }
};

#endif // VMSC_H
