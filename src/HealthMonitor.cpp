//
//  HealthMonitor.cpp
//
//  Created by Liudmyla Patenko on 03.02.2023.
//

#include "HealthMonitor.h"
#include "Logger.hpp"

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

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
            return response.id();
        } else {
            LOG_DEBUG << status.error_code() << ": " << status.error_message();
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
    // adding master
    nodes_.push_back({"", SecondaryStatus::HEALTHY, 0, 0});
}

HealthMonitor& HealthMonitor::getInstance() {
    static HealthMonitor instance;
    return instance;
}

void HealthMonitor::init(const std::vector<std::string> secondaries, const uint64_t timeout)
{
    timeout_ = timeout;

    for (std::string host : secondaries)
    {
        nodes_.push_back({host, SecondaryStatus::UNHEALTHY, -1, -1});
    }
}

void HealthMonitor::startMonitor()
{
    if (nodes_.size() == 1)
    {
        LOG_ERROR << "list of monitored secondary hosts is empty, cannot start health checking";
        return;
    }
    running_ = true;

    thread_ = std::thread([&]() { monitorSecondaries(); });
}

void HealthMonitor::setCallback(std::function<void(SecondaryStatus)> callback) {
    callback_ = callback;
}

void HealthMonitor::monitorSecondaries()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(timeout_));
        checkForInconsistency();
    }
    LOG_INFO << "monitorSecondaries Stopped"; 
}

int64_t HealthMonitor::getLastId(const std::string& secondary) {
    IdChecker checker{grpc::CreateChannel(secondary, grpc::InsecureChannelCredentials())};
    int64_t id = checker.getId();
    return id;
}

void HealthMonitor::checkForInconsistency()
{
    // TODO: double check requirements.
    // with current implementation, after adding arificial sleep in ReplicateService
    // health check will be OK
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& node : nodes_)
    {
        // do not check master
        if (node.hostname.empty()) continue;

        node.last_id = getLastId(node.hostname);

        bool prevStatus = node.status;

        if (node.last_id < 0) // secondary is not available
        {
            // secondary is already inactive for a long time, do nothig
            if (node.status == SecondaryStatus::UNHEALTHY) continue;

            node.inactive_count++;
            if (prevStatus == SecondaryStatus::HEALTHY)
            {
                // setting suspected state, but do nothing, maybe it is a network issue
                node.status = SecondaryStatus::SUSPECTED;
                continue;
            }

            // secondary is confirmed to be unhealthy
            if (node.inactive_count > 5)
            {
                node.status = SecondaryStatus::UNHEALTHY;
                node.inactive_count = -1;
            }
        }
        else // secondary is healthy
        {
            LOG_DEBUG << "Secondary '" << node.hostname << "' is alive! Last consecutive message id: " << node.last_id << "; Last id on master: " << getMasterNode().last_id;
            node.inactive_count = 0;
            node.status = SecondaryStatus::HEALTHY;
        }

        if ((node.status !=  SecondaryStatus::SUSPECTED && prevStatus != node.status) ||
            (node.status == SecondaryStatus::HEALTHY && getMasterNode().last_id != node.last_id))
        {
            LOG_DEBUG << "Status or consistency of the node: " << node.hostname << " has changed";
            condition_changed_ = true;
            nodes_changed_.push_back(node);
            cv_.notify_one();
        }
    }
}

void HealthMonitor::waitForStatusChange() {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this] { return (condition_changed_) || (!running_); });

    if (!running_) return;
    
    for (auto& secondary : nodes_changed_)
    {
        if (callback_) {
            callback_(secondary);
        }
    }
    nodes_changed_.clear();
    condition_changed_ = false;
}

void HealthMonitor::stopMonitor()
{
    LOG_INFO << "stopMonitor";

    running_ = false;
    cv_.notify_all();

    thread_.join();
    LOG_INFO << "MonitorSecondares joined"; 
}

bool HealthMonitor::isRunning()
{
    return running_;
}

std::string HealthMonitor::getStatus(const std::string hostname)
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& node : nodes_)
    {
        if (node.hostname == hostname)
            switch (node.status)
            {
                case SecondaryStatus::HEALTHY:
                    return "Healthy";
                case SecondaryStatus::SUSPECTED:
                    return "Suspected";
                case SecondaryStatus::UNHEALTHY:
                    return "Unhealthy";
                default:
                    return "Undefined";
            }
    }
    return "Undefined";
}

void HealthMonitor::setGlobalLastMessageId(size_t last_id)
{
    getMasterNode().last_id = last_id;
}

SecondaryStatus& HealthMonitor::getMasterNode()
{
    // currently master is always 1st element
    return nodes_[0];
}
