#include "vmsc.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void demonstrateVMSC() {
    std::cout << "========================================" << std::endl;
    std::cout << "Virtual Mobile Switching Center (vMSC)" << std::endl;
    std::cout << "Demonstration Program" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Create and initialize vMSC
    VMSC vmsc("vMSC-001");
    
    if (!vmsc.initialize()) {
        std::cerr << "Failed to initialize vMSC" << std::endl;
        return;
    }
    
    if (!vmsc.start()) {
        std::cerr << "Failed to start vMSC" << std::endl;
        return;
    }
    
    // Register subscribers
    std::cout << "\n--- Registering Subscribers ---" << std::endl;
    vmsc.registerSubscriber("310150123456789", "+14155551234");
    vmsc.registerSubscriber("310150987654321", "+14155555678");
    vmsc.registerSubscriber("310150111222333", "+14155559999");
    
    // Authenticate subscribers
    std::cout << "\n--- Authenticating Subscribers ---" << std::endl;
    vmsc.authenticateSubscriber("310150123456789");
    vmsc.authenticateSubscriber("310150987654321");
    
    // Update subscriber locations
    std::cout << "\n--- Updating Subscriber Locations ---" << std::endl;
    vmsc.updateSubscriberLocation("310150123456789", "LAC-100");
    vmsc.updateSubscriberLocation("310150987654321", "LAC-101");
    
    // Initiate calls
    std::cout << "\n--- Initiating Calls ---" << std::endl;
    std::string callId1 = vmsc.initiateCall("310150123456789", "+14155555678");
    std::string callId2 = vmsc.initiateCall("310150987654321", "+14155551234");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Answer calls
    std::cout << "\n--- Answering Calls ---" << std::endl;
    if (!callId1.empty()) {
        vmsc.answerCall(callId1);
    }
    if (!callId2.empty()) {
        vmsc.answerCall(callId2);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Perform handover
    std::cout << "\n--- Performing Handover ---" << std::endl;
    vmsc.performHandover("310150123456789", "LAC-100", "LAC-102");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Print status
    vmsc.printStatus();
    
    // End calls
    std::cout << "\n--- Ending Calls ---" << std::endl;
    if (!callId1.empty()) {
        vmsc.endCall(callId1);
    }
    if (!callId2.empty()) {
        vmsc.endCall(callId2);
    }
    
    // Final status
    vmsc.printStatus();
    
    // Stop vMSC
    std::cout << "\n--- Shutting Down ---" << std::endl;
    vmsc.stop();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Demonstration Complete" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        demonstrateVMSC();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
