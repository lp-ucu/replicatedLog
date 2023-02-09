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
#include <vector>

struct SecondaryStatus
{
    std::string hostname;
    enum {HEALTHY, SUSPECTED, UNHEALTHY} status;
    int64_t inactive_count;
    int64_t last_id;
};

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
    void setCallback(std::function<void(SecondaryStatus)> callback);
    void waitForStatusChange();

    std::string getStatus(const std::string hostanme);
    void setGlobalLastMessageId(size_t last_id);

private:
    HealthMonitor();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    volatile bool condition_changed_;
    volatile bool running_;
    uint64_t timeout_;
    std::string health_service_name_;

    std::vector<SecondaryStatus> nodes_;
    std::vector<SecondaryStatus> nodes_changed_;

    std::function<void(SecondaryStatus)> callback_;

    void monitorSecondaries();
    void checkForInconsistency();
    int64_t getLastId(const std::string& secondary);
    SecondaryStatus& getMasterNode();
};

#endif /* HealthMonitor_h */
