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
#include "params.hpp"

SParameters params = {
    true,
    "",
    80,
    "1234",
    {}
};
// SParameters params = { c++ 20
//     .isMaster=true,
//     .hostname="localhost",
//     .http_port=80,
//     .rpc_port="50051"
//     };

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

std::vector<int64_t> results;
void replicateMessageRPC(const std::string& message, const size_t id, const std::string& slave_address, int i) {
    ReplicatedLogMaster master{grpc::CreateChannel(slave_address, grpc::InsecureChannelCredentials())};
    int64_t res = master.appendMessage(id, message);
    LOG_DEBUG << "Response from slave: " << res;
    results[i] = res;
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
                // replicateMessageRPC(x["message"].s(), local_id);
                // std::vector<std::tuple<std::thread, int>> replicate_threads;

                // std::pair<std::thread, int>
                std::vector<std::thread> replicate_threads;
                // std::vector<int64_t> results;

                // for(std::string& slave:params.slaves)
                for(int i = 0; i < params.slaves.size(); i++)
                {   
                    results.push_back(0);                       
                    replicate_threads.push_back(std::thread(replicateMessageRPC, x["message"].s(), local_id, params.slaves[i], i));
                }
                // replicate_threads.push_back(std::pair(std::thread(replicateMessageRPC, x["message"].s(),local_id, slave), 0));

                bool ok = true;
                // for(std::thread& repl_thr:replicate_threads)
                for(int i = 0; i < params.slaves.size(); i++)
                {
                    replicate_threads[i].join();
                    std::cout<<results[i]<<std::endl;
                    if(results[i] != 0)
                    {
                        ok = false;
                    }
                }
                if(ok)
                {
                    return crow::response{201};
                }
                else
                {
                    return crow::response{500};
                } });
    }
    
    uint64_t port = params.http_port;
    
    // HTTP server will use std::thread::hardware_concurrency() number of threads threads
    app.port(port).multithreaded().run();
}

int main(int argc, const char * argv[]) {
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
        httpThread.join();
    }
//TODO: dockerfile, docker composer
//TODO: refactor code to use classes
    return 0;
}
