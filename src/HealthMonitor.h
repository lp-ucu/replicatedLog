//
//  HealthMonitor.h
//  replicatedlog
//
//  Created by Liudmyla Patenko on 30.12.2022.
//

#ifndef HealthMonitor_h
#define HealthMonitor_h

#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "health.grpc.pb.h"


struct Secondary
{
    std::string host;
    bool active;
};

class HealthMonitor
{
public:
    HealthMonitor(const std::vector<std::string> secondaries, const std::string health_service_name, const uint64_t timout = 5);
    
//    void registerSecondary(const std::string address);
    void startMonitor();
    void stopMonitor();
    bool isRunning();
    
    std::vector<Secondary> getOverallStatus();
    bool getStatus(const std::string secondary_host);

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    bool running_;
    uint64_t timout_;
    std::string health_service_name_;
    std::vector<Secondary> secondaries_;
    
    std::function<void(int)> callback_;
    
    void monitorSecondaries();
    void sendHeartbeat();


};

#endif /* HealthMonitor_h */
