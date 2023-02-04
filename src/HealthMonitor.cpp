//
//  HealthMonitor.cpp
//  health_grpc_proto
//
//  Created by Liudmyla Patenko on 03.02.2023.
//

#include "HealthMonitor.h"
#include "Logger.hpp"

#include <iostream>

#include <grpcpp/grpcpp.h>
#include "health.grpc.pb.h"

using grpc::ClientContext;
using grpc::Status;
using grpc::Channel;

using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

HealthMonitor::HealthMonitor(const std::vector<std::string> secondaries, const std::string health_service_name, const uint64_t timeout):
    health_service_name_(health_service_name),
    timout_(timeout),
    running_(false)
{
    for (std::string host : secondaries)
    {
        Secondary sec;
        sec.host = host;
        sec.active = false;
        secondaries_.push_back(sec);
    }
}

void HealthMonitor::startMonitor()
{
    running_ = true;
    auto shared_this = std::shared_ptr<HealthMonitor>(this);
    thread_ = std::thread([shared_this]() { shared_this->monitorSecondaries(); });

    //thread_ = std::thread(&HealthMonitor::monitorSecondaries, this);
    thread_.join(); // TODO: not possible to stop the timer!!
}

void HealthMonitor::monitorSecondaries()
{
    while (running_)
    {
        LOG_INFO << "monitorSecondaries";
        std::this_thread::sleep_for(std::chrono::seconds(20));
        // TODO: mutex!!!
        sendHeartbeat();
    }
}

void HealthMonitor::sendHeartbeat()
{
    // TODO: double check requirements.
    // with current implementation, after adding arificial sleep in ReplicateService
    // health check will be OK
    for (Secondary secondary : secondaries_)
    {
        HealthCheckRequest request;
        request.set_service(health_service_name_);
        
        LOG_INFO << "sending heartbeat to " << secondary.host;
        
        HealthCheckResponse response;
        ClientContext context;
        std::shared_ptr<Channel> channel = CreateChannel(secondary.host, grpc::InsecureChannelCredentials());
        std::unique_ptr<Health::Stub> hc_stub = grpc::health::v1::Health::NewStub(channel);
        Status s = hc_stub->Check(&context, request, &response);
        if (s.ok()) {
            LOG_INFO << "service " << secondary.host << " is OK";
            secondary.active = true;
        }
        else
        {
            LOG_INFO << "service " << secondary.host << " is not responding";
            secondary.active = false;
        }
    }
}

void HealthMonitor::stopMonitor()
{
  running_ = false;
  thread_.join();
}

bool HealthMonitor::isRunning()
{
    return running_;
}

std::vector<Secondary> HealthMonitor::getOverallStatus()
{
    return secondaries_;
}
bool HealthMonitor::getStatus(const std::string secondary_host)
{
    for (Secondary secondary : secondaries_)
    {
        if (secondary.host == secondary_host)
            return secondary.active;
    }
    return false;
}


