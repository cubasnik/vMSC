# vMSC - Virtual Mobile Switching Center

A C++ implementation of a virtual Mobile Switching Center (MSC), which is a critical component in mobile telecommunications networks for routing and managing voice and data traffic.

## Features

- **Subscriber Management**: Register and authenticate mobile subscribers
- **Call Management**: Setup, connect, and terminate calls between subscribers
- **Mobility Management**: Handle handovers between different location areas
- **Thread-safe Operations**: All operations are protected with mutexes for concurrent access

## Architecture

The vMSC consists of four main components:

1. **SubscriberManager**: Manages subscriber registration, authentication, and location updates
2. **CallManager**: Handles call setup, connection, and termination
3. **MobilityManager**: Manages handovers between location area codes (LACs)
4. **VMSC**: Main class that coordinates all components

## Building

### Prerequisites

- CMake 3.10 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)

### Build Instructions

```bash
# Create build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# The executable will be in build/bin/vmsc
```

## Usage

Run the demonstration program:

```bash
./build/bin/vmsc
```

The demonstration program will:
1. Initialize and start the vMSC
2. Register multiple subscribers
3. Authenticate subscribers
4. Update subscriber locations
5. Initiate and connect calls
6. Perform handovers between location areas
7. Display status reports
8. Terminate calls and shut down

## Example Output

```
========================================
Virtual Mobile Switching Center (vMSC)
Demonstration Program
========================================

Initializing vMSC: vMSC-001
Starting vMSC: vMSC-001

--- Registering Subscribers ---
Subscriber registered: IMSI=310150123456789, MSISDN=+14155551234
...

--- Active Calls ---
Call ID         Caller IMSI         Callee          State
-----------------------------------------------------------------
CALL-00000001   310150123456789     +14155555678    CONNECTED
...
```

## API Overview

### Subscriber Operations

```cpp
// Register a new subscriber
bool registerSubscriber(const std::string& imsi, const std::string& msisdn);

// Authenticate a subscriber
bool authenticateSubscriber(const std::string& imsi);

// Update subscriber location
bool updateSubscriberLocation(const std::string& imsi, const std::string& location);
```

### Call Operations

```cpp
// Initiate a call
std::string initiateCall(const std::string& callerImsi, const std::string& calleeNumber);

// Answer/connect a call
bool answerCall(const std::string& callId);

// End a call
bool endCall(const std::string& callId);
```

### Mobility Operations

```cpp
// Perform handover between location areas
bool performHandover(const std::string& imsi, const std::string& sourceLac, const std::string& targetLac);
```

## License

This project is provided as-is for educational and demonstration purposes.