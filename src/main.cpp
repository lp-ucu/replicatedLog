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
#include <string>

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"

#include "crow_all.h" //http server
#include "Logger.hpp"
#include "HealthMonitor.h"
#include "params.hpp"
#include "grpc_request.hpp"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using replicatedlog::ReplicateService;
using replicatedlog::ReplicateResponce;
using replicatedlog::MessageItem;
using replicatedlog::LastMessageId;
using replicatedlog::ReplicateParamStub;

SParameters params = {0};
std::atomic<size_t> id_count{0};
std::set<std::tuple<int64_t, std::string>> messages;
std::mutex mu;

int saveMessage(std::string message, size_t id)
{
    LOG_INFO << "saveMessage '" << message << "' with id: " << id;
    {
        std::lock_guard<std::mutex> lock(mu);
        // idempotent operation
        messages.insert(std::tuple<int64_t, std::string>(id, message));
    }

    return 0;
}

class ReplicateServiceImpl final : public ReplicateService::Service {
    Status appendMessage(ServerContext* context, const MessageItem* message, ReplicateResponce* response) override {
        saveMessage(message->text(), message->id());
        response->set_res(0);
        return Status::OK;
    }

    Status getLastMessageId(ServerContext* context, const ReplicateParamStub* param, LastMessageId* message) override {
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

void startHttpServer(crow::SimpleApp app, bool isMaster)
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

                size_t local_id = ++id_count;
                size_t w_concern = x["w"].u();

                LOG_DEBUG << "received POST with message " << x["message"].s() << " and wite concern: " << w_concern;
                saveMessage(x["message"].s(), local_id);
                HealthMonitor::getInstance().setGlobalLastMessageId(local_id);

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

                HealthMonitor& monitor = HealthMonitor::getInstance();

                for (auto secondary : params.slaves)
                {
                    x[secondary] = monitor.getStatus(secondary);
                }

                return x;
            });
    }
    
    uint64_t port = params.http_port;
    
    // HTTP server will use std::thread::hardware_concurrency() number of threads threads
    app.port(port).multithreaded().run();
}

int main(int argc, const char * argv[]) {

    if (!parse_args(argc, argv, &params))
    {
        return -1;
    }

    //set filename if need to redirect all logs to file
    //std::string filename = std::string("log_") + params.hostname + "_" + std::to_string(params.http_port);
    //Logger logger(filename);

    LOG_INFO << "Starting replicated log process";

    LOG_INFO << "Running as " << (params.isMaster ? "master" : "slave");

    crow::SimpleApp app;
    std::thread httpThread(startHttpServer, app, params.isMaster);
    if(!params.isMaster)
    {
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
        monitor.setCallback([](SecondaryStatus secondary) {
            LOG_INFO << "Status of secondary " << secondary.hostname << " has changed to " << secondary.status << " last id: " << secondary.last_id;
            if ((secondary.status != SecondaryStatus::UNHEALTHY))
            {
                int64_t next_msg_id = secondary.last_id + 1;
                const std::string* next_msg = 0;

                for (auto &[id, msg] : messages)
                {
                    if (id == next_msg_id)
                    {
                        next_msg = &msg;
                        break;
                    }
                }
                if (!next_msg) return;

                SyncMessage(next_msg_id, *next_msg, secondary.hostname);
            }
          });

        std::thread healthStatusThread([&monitor] {
            while (monitor.isRunning())
            {
                monitor.waitForStatusChange();
            }
            LOG_INFO << "healthStatusThread stopped..";
        });

        monitor.startMonitor();

        httpThread.join();
        LOG_INFO << "httpThread stopped..";

        monitor.stopMonitor();

        healthStatusThread.join();
    }
    return 0;
}
