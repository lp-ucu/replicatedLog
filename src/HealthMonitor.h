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
#include <map>
#include <vector>

class HealthMonitor
{
public:
    static HealthMonitor& getInstance();

    HealthMonitor(HealthMonitor const&) = delete;
    void operator=(HealthMonitor const&) = delete;

    void init(const std::vector<std::string> secondaries, const uint64_t timeout = 5);
    void startMonitor();
    void stopMonitor();
    bool isRunning();
    void setCallback(std::function<void(std::pair<std::string, bool> secondary)> callback);
    void waitForStatusChange();
    int64_t getLastId(const std::string& secondary);

    std::map<std::string, bool> getOverallStatus();
    bool getStatus(const std::string secondary_host);

private:
    HealthMonitor();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    volatile bool condition_changed_;
    volatile bool running_;
    uint64_t timeout_;
    std::string health_service_name_;

    std::map<std::string, bool> secondaries_;
    std::map<std::string, bool> sec_changed_;

    std::function<void(std::pair<std::string, bool>)> callback_;

    void monitorSecondaries();
    void sendHeartbeat();
};

#endif /* HealthMonitor_h */
