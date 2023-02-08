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

#include "crow_all.h" //http server
#include "Logger.hpp"
#include "HealthMonitor.h"
#include "params.hpp"
#include "grpc_request.hpp"



SParameters params = {0};

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
using replicatedlog::LastMessageId;
using replicatedlog::ReplicateParamStub;


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

    Status getLastMessageId(ServerContext* context, const ReplicateParamStub* param, LastMessageId* message) override {
        LOG_DEBUG << "getLastMessageId: call";
        if (messages.size())
        {
            size_t prevId = 0;
            std::lock_guard<std::mutex> lock(mu);
            for (auto it = messages.begin(); it != messages.end(); ++it) {
                if (std::get<0>(*it)-1 != prevId)
                    break;
                prevId++;
            }
            message->set_id(prevId);
        }
        else
        {
            message->set_id(0);
        }
        return Status::OK;
    }
};

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

                bool ok = ReplicateMessage(local_id, x["message"].s(), params.slaves, w_concern - 1);

                if(ok)
                {
                    return crow::response{201};
                }
                else
                {
                    return crow::response{500};
                } });

        CROW_ROUTE(app, "/health")
            .methods("GET"_method)([](const crow::request& req) {
                crow::json::wvalue x;

                std::vector<std::string> hosts{"localhost:2510"};
                const grpc::string kHealthyService("healthy_service");

                HealthMonitor& monitor = HealthMonitor::getInstance();

                for (auto secondary : monitor.getOverallStatus())
                {
                    x[secondary.first] = secondary.second;
                }

                return x;
            });
    }
    
    uint64_t port = params.http_port;
    
    // HTTP server will use std::thread::hardware_concurrency() number of threads threads
    app.port(port).multithreaded().run();
}

int main(int argc, const char * argv[]) {
    //set filename if need to redirect all logs to file
    Logger logger("");

    LOG_INFO << "Starting replicated log process";
    //set filename if need to redirect all logs to file
    if (!parse_args(argc, argv, &params))
    {
        return -1;
    }

    LOG_INFO << "Running as " << (params.isMaster ? "master" : "slave");

    std::thread httpThread(startHttpServer, params.isMaster);
    if(!params.isMaster)
    {
        // std::thread serviceThread(runReplicateService, params.hostname, params.rpc_port);
        std::string server_address{params.hostname};
        server_address.append(":").append(params.rpc_port);
        ReplicateServiceImpl service;

        // Build server
        ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        // Run server
        std::unique_ptr<Server> replicate_server{builder.BuildAndStart()};
        LOG_INFO << "replicate service is listening on " << server_address;
        // Wait for httpThread to exit here so that replicate_server is not destroyed
        httpThread.join();
    }
    else
    {
        HealthMonitor& monitor = HealthMonitor::getInstance();
        monitor.init(params.slaves, 5);
        monitor.setCallback([](std::pair<std::string, bool> secondary) {
            LOG_INFO << "Status of secondary " << secondary.first << " has changed to " << (secondary.second ? "running" : "not available");
          });

        std::thread healthStatusThread([&monitor] {
            while (monitor.isRunning())
            {
                // LOG_INFO << "healthStatusThread started..";
                monitor.waitForStatusChange();
                LOG_INFO << "healthStatusThread ok..";
            }
            LOG_INFO << "healthStatusThread stopped..";
        });

        monitor.startMonitor();

        httpThread.join();
        LOG_INFO << "httpThread stopped..";

        monitor.stopMonitor();

        healthStatusThread.join();
    }
//TODO: dockerfile, docker composer
//TODO: refactor code to use classes
    return 0;
}
