//
//  HealthMonitor.cpp
//  health_grpc_proto
//
//  Created by Liudmyla Patenko on 03.02.2023.
//

#include "HealthMonitor.h"
#include "Logger.hpp"

#include <grpcpp/grpcpp.h>
#include "health.grpc.pb.h"
#include "replicate.grpc.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

using replicatedlog::ReplicateService;
using replicatedlog::LastMessageId;
using replicatedlog::ReplicateParamStub;

class IdChecker {
public:
    IdChecker(std::shared_ptr<Channel> channel) :
    _stub{ReplicateService::NewStub(channel)} {}

    int64_t getId() {
        // Prepare request
        ReplicateParamStub stub;

        // Send request
        LastMessageId response;
        ClientContext context;
        Status status;
        status = _stub->getLastMessageId(&context, stub, &response);

        // Handle response
        if (status.ok()) {
            LOG_DEBUG << "Service is alive! Last message id: '" << response.id();
            return response.id();
        } else {
            LOG_ERROR << status.error_code() << ": " << status.error_message();
            return -1;
        }
    }

private:
    std::unique_ptr<ReplicateService::Stub> _stub;
};

HealthMonitor::HealthMonitor():
running_(false),
condition_changed_(false)
{
}

HealthMonitor& HealthMonitor::getInstance() {
    static HealthMonitor instance;
    return instance;
}

void HealthMonitor::init(const std::vector<std::string> secondaries, const std::string health_service_name, const uint64_t timeout)
{
    health_service_name_ = health_service_name;
    timeout_ = timeout;

    for (std::string host : secondaries)
    {
        secondaries_.insert({host, false});
    }
}

void HealthMonitor::startMonitor()
{
    if (secondaries_.size() == 0)
    {
        LOG_ERROR << "list of monitored hosts is empty, cannot start health checking";
        return;
    }
    running_ = true;
    auto shared_this = std::shared_ptr<HealthMonitor>(this);
    thread_ = std::thread([shared_this]() { shared_this->monitorSecondaries(); });

    thread_.join(); // TODO: not possible to stop the timer!!
}

void HealthMonitor::setCallback(std::function<void(std::pair<std::string, bool>)> callback) {
    callback_ = callback;
}

void HealthMonitor::monitorSecondaries()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(timeout_));
        sendHeartbeat();
    }
}

int64_t HealthMonitor::getLastId(const std::string& secondary) {
    IdChecker checker{grpc::CreateChannel(secondary, grpc::InsecureChannelCredentials())};
    int64_t id = checker.getId();
    return id;
}

void HealthMonitor::sendHeartbeat()
{
    // TODO: double check requirements.
    // with current implementation, after adding arificial sleep in ReplicateService
    // health check will be OK
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& secondary : secondaries_)
    {
        HealthCheckRequest request;
        request.set_service(health_service_name_);

        LOG_INFO << "sending heartbeat to " << secondary.first;

        HealthCheckResponse response;
        ClientContext context;
        std::shared_ptr<Channel> channel = CreateChannel(secondary.first, grpc::InsecureChannelCredentials());
        std::unique_ptr<Health::Stub> hc_stub = grpc::health::v1::Health::NewStub(channel);
        Status s = hc_stub->Check(&context, request, &response);

        int64_t last_id = getLastId(secondary.first);
        LOG_INFO << "service " << secondary.first << " returned last message id: " << last_id;

        bool prevStatus = secondary.second;
        if (s.ok()) {
            LOG_INFO << "service " << secondary.first << " is OK";
            secondary.second = true;
        }
        else
        {
            LOG_INFO << "service " << secondary.first << " is not responding";
            secondary.second = false;
        }

        if (prevStatus != secondary.second)
        {
            LOG_INFO << "status has changed";
            condition_changed_ = true;
            sec_changed_.insert(secondary);
            cv_.notify_one();
        }
    }
}

void HealthMonitor::waitForStatusChange() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return condition_changed_; });
    for (auto secondary : sec_changed_)
    {
        LOG_INFO << "waitForStatusChange: status changed for " << secondary.first;
        if (callback_) {
            callback_(secondary);
        }
    }
    sec_changed_.clear();
    condition_changed_ = false;
}

void HealthMonitor::stopMonitor()
{
    // TODO: need to fix, currently does not work
    running_ = false;
    thread_.join();
}

bool HealthMonitor::isRunning()
{
    return running_;
}

std::map<std::string, bool> HealthMonitor::getOverallStatus()
{
    std::unique_lock<std::mutex> lock(mutex_);
    return secondaries_;
}

bool HealthMonitor::getStatus(const std::string secondary_host)
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& secondary : secondaries_)
    {
        if (secondary.first == secondary_host)
            return secondary.second;
    }
    return false;
}
