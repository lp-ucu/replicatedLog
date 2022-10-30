//
//  main.cpp
//  replicatedlog
//
//  Created by Liudmyla Patenko on 23.10.2022.
//

#include <iostream>
#include <thread>

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"

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

bool isMaster = false;
crow::SimpleApp app;
//uint64_t id_count = 0;
int64_t id_count = 0;

//std::map<uint64_t, std::string> messages;
std::vector<crow::json::wvalue> messages;


class ReplicateServiceImpl final : public ReplicateService::Service {
    Status appendMessage(ServerContext* context, const MessageItem* message, ReplicateResponce* response) override {
        LOG_DEBUG << "replicating message: '" << message->text() << "' with id: " << message->id();
        messages.push_back(message->text());
        response->set_res(0);
        return Status::OK;
    }
};

void runReplicateService(bool isMaster, const std::string port) {
    if (isMaster)
        return;
    
    std::string server_address{"localhost:"};
    server_address.append(port);
    ReplicateServiceImpl service;
    
    // Build server
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server{builder.BuildAndStart()};
    
    // Run server
    LOG_INFO << "replicate service is listening on " << server_address << ":" << port;
    server->Wait();
}

class ReplicatedLogMaster {
public:
    ReplicatedLogMaster(std::shared_ptr<Channel> channel) :
    _stub{ReplicateService::NewStub(channel)} {}

    int64_t appendMessage(const int64_t id, const std::string& text) {
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

int saveMessage(std::string message, int64_t id)
{
    LOG_DEBUG << "saveMessage '" << message << "' with id: " ;
//    messages.insert(std::pair<uint64_t,std::string>(id_count, message));
//TODO: make threadsafe
    messages.push_back(message);
    return 0;
}

void replicateMessageRPC(const std::string& message, const int64_t id) {
    std::string slave_address{"localhost:"};
    slave_address.append(SLAVE_RPC_PORT);
    ReplicatedLogMaster master{grpc::CreateChannel(slave_address, grpc::InsecureChannelCredentials())};
    int64_t res = master.appendMessage(id, message);
    LOG_DEBUG << "Response from slave: ";
}

void startHttpServer(bool isMaster)
{
    CROW_ROUTE(app, "/messages")
        .methods("GET"_method)([](const crow::request& req) {
            crow::json::wvalue x = crow::json::wvalue::list(messages);
            return x;
    });
    
    if (isMaster) {
        CROW_ROUTE(app, "/message")
            .methods("POST"_method)([](const crow::request& req) {
                auto x = crow::json::load(req.body);
                
                if (!x)
                    return crow::response(400);
                else if (!x.has("message"))
                    return crow::response(400);
                
                //          std::cout << "1 " << x["message"].s() << std::endl;
                //          sleep(10);
                //          std::cout << "2 " << x["message"].s() << std::endl;
                LOG_DEBUG << "received POST with message " << x["message"].s();
//TODO: id_count make atomic, static, not global,...
//TODO: int local_id = ++id_count;
                saveMessage(x["message"].s(), ++id_count);
//TODO: config of our dist system (how many slaves, ports, hostnames/ip)
//TODO: send rpc in separate threads if confirmed
                replicateMessageRPC(x["message"].s(), id_count);
                return crow::response{201};
            });
    }
    
    uint64_t port = isMaster ? MASTER_HTTP_PORT : SLAVE_HTTP_PORT;
    app.port(port).multithreaded().run();
}

int main(int argc, const char * argv[]) {
    //set filename if need to redirect all logs to file
    Logger logger(REDIRECT_LOG_FILE);

    LOG_INFO << "Starting replicated log process";
    if (argc == 1 || std::string(argv[1]) == "-m")
        isMaster = true;
    LOG_INFO << "Running as " << (isMaster ? "master" : "slave");

    std::thread httpThread(startHttpServer, isMaster);
    std::thread serviceThread(runReplicateService, isMaster, SLAVE_RPC_PORT);

    serviceThread.join();
    httpThread.join();

//TODO: dockerfile, docker composer
//TODO: refactor code to use classes
    return 0;
}
