#ifndef SUBSCRIBER_MANAGER_H
#define SUBSCRIBER_MANAGER_H

#include <string>
#include <unordered_map>
#include <mutex>

struct Subscriber {
    std::string imsi;
    std::string msisdn;
    std::string location;
    bool isActive;
    
    Subscriber(const std::string& imsi, const std::string& msisdn)
        : imsi(imsi), msisdn(msisdn), location(""), isActive(false) {}
};

class SubscriberManager {
private:
    std::unordered_map<std::string, Subscriber> subscribers;
    mutable std::mutex mutex_;

public:
    SubscriberManager() = default;
    
    bool addSubscriber(const std::string& imsi, const std::string& msisdn);
    bool authenticateSubscriber(const std::string& imsi);
    bool updateLocation(const std::string& imsi, const std::string& location);
    bool isSubscriberActive(const std::string& imsi) const;
    void printSubscribers() const;
};

#endif // SUBSCRIBER_MANAGER_H
