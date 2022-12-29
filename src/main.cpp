//
//  main.cpp
//  replicatedlog
//
//  Created by Liudmyla Patenko on 23.10.2022.
//

#include <iostream>
#include <thread>
#include <tuple>
#include <mutex>

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"
#include "health.grpc.pb.h"

#include "crow_all.h" //http server
#include "Logger.hpp"

#define MASTER_HTTP_PORT 18080
#define SLAVE_HTTP_PORT 28080
#define SLAVE_RPC_PORT "2510"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

using replicatedlog::ReplicateService;
using replicatedlog::ReplicateResponce;
using replicatedlog::MessageItem;

using grpc::health::v1::Health;
using grpc::health::v1::HealthCheckRequest;
using grpc::health::v1::HealthCheckResponse;

bool isMaster = false;
crow::SimpleApp app;
std::atomic<size_t> id_count{0};
std::mutex mu;

std::set<std::tuple<int64_t, std::string>> messages;

int saveMessage(std::string message, size_t id)
{
    LOG_DEBUG << "saveMessage '" << message << "' with id: " << id;
    {
        std::lock_guard<std::mutex> lock(mu);
        // idempotent operation
        messages.insert(std::tuple<int64_t, std::string>(id, message));
    }

    return 0;
}

class ReplicateServiceImpl final : public ReplicateService::Service {
    Status appendMessage(ServerContext* context, const MessageItem* message, ReplicateResponce* response) override {
        LOG_DEBUG << "replicating message: '" << message->text() << "' with id: " << message->id();
        saveMessage(message->text(), message->id());
        response->set_res(0);
        return Status::OK;
    }
};

void runReplicateService(bool isMaster, const std::string port, const grpc::string healthy_service_name) {
    if (isMaster)
        return;
    
    std::string server_address{"localhost:"};
    server_address.append(port);

    // Enable native grpc health check
    grpc::EnableDefaultHealthCheckService(true);

    ReplicateServiceImpl service;
    
    // Build server
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server{builder.BuildAndStart()};
    
    // Setup health service
    grpc::HealthCheckServiceInterface* health_service = server->GetHealthCheckService();
    health_service->SetServingStatus(healthy_service_name, true);

    // Run server
    LOG_INFO << "replicate service is listening on " << server_address;
    server->Wait();
}

class ReplicatedLogMaster {
public:
    ReplicatedLogMaster(std::shared_ptr<Channel> channel) :
    _stub{ReplicateService::NewStub(channel)} {}

    int64_t appendMessage(const size_t id, const std::string& text) {
        // Prepare request
        MessageItem message;
        message.set_id(id);
        message.set_text(text);

        // Send request
        ReplicateResponce response;
        ClientContext context;
        Status status;
        status = _stub->appendMessage(&context, message, &response);

        // Handle response
        if (status.ok()) {
            LOG_DEBUG << "message '" << text << "' is succefully replicated";
            return response.res();
        } else {
            LOG_ERROR << status.error_code() << ": " << status.error_message();
            return -1;
        }
    }

private:
    std::unique_ptr<ReplicateService::Stub> _stub;
};

void replicateMessage(std::string message)
{
    LOG_INFO << "replicateMessage " << message;
    return;
}


void replicateMessageRPC(const std::string& message, const size_t id) {
    std::string slave_address{"localhost:"};
    slave_address.append(SLAVE_RPC_PORT);
    ReplicatedLogMaster master{grpc::CreateChannel(slave_address, grpc::InsecureChannelCredentials())};
    int64_t res = master.appendMessage(id, message);
    LOG_DEBUG << "Response from slave: " << res;
}

void startHttpServer(bool isMaster)
{
    CROW_ROUTE(app, "/messages")
        .methods("GET"_method)([](const crow::request& req) {
            std::vector<crow::json::wvalue> msgList;
            size_t prevId = 0;
            {
                std::lock_guard<std::mutex> lock(mu);
                for (auto it = messages.begin(); it != messages.end(); ++it) {
                    if (std::get<0>(*it)-1 != prevId)
                        break;
                    prevId++;
                    msgList.push_back(std::get<1>(*it));
                }
            }
            crow::json::wvalue x = crow::json::wvalue::list(msgList);
            return x;
    });
    
    if (isMaster) {
        CROW_ROUTE(app, "/message")
            .methods("POST"_method)([](const crow::request& req) {
                auto x = crow::json::load(req.body);
                
                if (!x)
                    return crow::response(400, "Request body is missing");
                else if (!x.has("message") || !x.has("w"))
                    return crow::response(400, "'message' or 'w' parameter is missing");
                else if (x["w"].u() <= 0 || x["w"].u() > 3)
                    return crow::response(400, "Bad write concern parameter. Allowed values: 1,2,3");

//TODO: id_count make, static, not global,...
                size_t local_id = ++id_count;
                size_t w_concern = x["w"].u();

                LOG_DEBUG << "received POST with message " << x["message"].s() << " and wite concern: " << w_concern;

                saveMessage(x["message"].s(), local_id);
//TODO: send rpc in separate threads if confirmed
                replicateMessageRPC(x["message"].s(), local_id);
                return crow::response{201};
            });
    }
    
    uint64_t port = isMaster ? MASTER_HTTP_PORT : SLAVE_HTTP_PORT;
    // HTTP server will use std::thread::hardware_concurrency() number of threads threads
    app.port(port).multithreaded().run();
}

void sendHeartbeat(const grpc::string healthy_service_name, const std::string server_port)
{
    std::string server_address{"localhost:"};
    server_address.append(server_port);

    HealthCheckRequest request;
    request.set_service(healthy_service_name);

    HealthCheckResponse response;
    ClientContext context;
    std::shared_ptr<Channel> channel = CreateChannel(server_address, grpc::InsecureChannelCredentials());
    std::unique_ptr<Health::Stub> hc_stub = grpc::health::v1::Health::NewStub(channel);
    Status s = hc_stub->Check(&context, request, &response);
    if (s.ok()) {
        LOG_INFO << "service is OK";
    }
}

int main(int argc, const char * argv[]) {
    //set filename if need to redirect all logs to file
    Logger logger("");
    const grpc::string kHealthyService("healthy_service");


    LOG_INFO << "Starting replicated log process";
    if (argc == 1 || std::string(argv[1]) == "-m")
        isMaster = true;
    LOG_INFO << "Running as " << (isMaster ? "master" : "slave");

    std::thread httpThread(startHttpServer, isMaster);
    std::thread serviceThread(runReplicateService, isMaster, SLAVE_RPC_PORT, kHealthyService);

    if (isMaster)
    {
        std::thread heartbeat(sendHeartbeat, kHealthyService, SLAVE_RPC_PORT);
        heartbeat.join();
    }

    serviceThread.join();
    httpThread.join();

//TODO: dockerfile, docker composer
//TODO: refactor code to use classes
    return 0;
}
