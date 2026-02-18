#ifndef CALL_MANAGER_H
#define CALL_MANAGER_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

enum class CallState {
    IDLE,
    SETUP,
    ALERTING,
    CONNECTED,
    DISCONNECTING,
    TERMINATED
};

struct Call {
    std::string callId;
    std::string callerImsi;
    std::string calleeNumber;
    CallState state;
    uint64_t setupTime;
    uint64_t connectTime;
    
    Call(const std::string& id, const std::string& caller, const std::string& callee)
        : callId(id), callerImsi(caller), calleeNumber(callee), 
          state(CallState::IDLE), setupTime(0), connectTime(0) {}
};

class CallManager {
private:
    std::unordered_map<std::string, std::shared_ptr<Call>> activeCalls;
    mutable std::mutex mutex_;
    uint64_t callCounter;

public:
    CallManager();
    
    std::string setupCall(const std::string& callerImsi, const std::string& calleeNumber);
    bool connectCall(const std::string& callId);
    bool terminateCall(const std::string& callId);
    CallState getCallState(const std::string& callId) const;
    void printActiveCalls() const;
    
private:
    std::string generateCallId();
};

#endif // CALL_MANAGER_H
