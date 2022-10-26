//
//  main.cpp
//  replicatedlog
//
//  Created by Liudmyla Patenko on 23.10.2022.
//

#include <iostream>
#include <grpc++/grpc++.h>
#include <memory>


#include "crow_all.h"

#include <grpcpp/grpcpp.h>
#include <grpc/grpc.h>

#include "replicate.grpc.pb.h"
//#include "message.pb.h"

//possibly client
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

//probably server
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;


using replicatedlog::ReplicateService;
using replicatedlog::ReplicateResponce;
using replicatedlog::MessageItem;
using replicatedlog::MessageItems;

bool isMaster = false;
crow::SimpleApp app;
//uint64_t id_count = 0;
int64_t id_count = 0;

//std::map<uint64_t, std::string> messages;
std::vector<crow::json::wvalue> messages;


class ReplicateServiceImpl final : public ReplicateService::Service {
    Status appendMessage(ServerContext* context, const MessageItem* message, ReplicateResponce* response) override {
        std::cout << "replicating message: '" << message->text() << "' with id: " << message->id() << std::endl;
        response->set_res(0);
        return Status::OK;
    }
};

void runReplicateService() {
    std::string server_address{"localhost:2510"};
    ReplicateServiceImpl service;
    
    // Build server
    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<Server> server{builder.BuildAndStart()};
    
    // Run server
    std::cout << "Replicate service is listening on " << server_address << std::endl;
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
            return response.res();
        } else {
            std::cerr << status.error_code() << ": " << status.error_message() << std::endl;
            return -1;
        }
    }

private:
    std::unique_ptr<ReplicateService::Stub> _stub;
};

void replicateMessage(std::string message)
{
    std::cout << "replicateMessage " << message << std::endl;
    return;
}

int saveMessage(std::string message)
{
    std::cout << "saveMessage " << message << std::endl;
//    messages.insert(std::pair<uint64_t,std::string>(id_count, message));
//    id_count++;
    messages.push_back(message);
    return 0;
}

void replicateMessageRPC(const std::string& message, const int64_t id) {
    std::string slave_address{"localhost:2510"};
    ReplicatedLogMaster master{grpc::CreateChannel(slave_address, grpc::InsecureChannelCredentials())};
    int64_t res = master.appendMessage(id, message);
    std::cout << "Responce from slave: " << res << std::endl;
}
void setupRoutes(bool isMaster)
{
    CROW_ROUTE(app, "/get_test")
      .methods("GET"_method)([](const crow::request& req) {
          std::ostringstream os;
          os << "testetsttest";
          return crow::response{os.str()};
      });
    
    CROW_ROUTE(app, "/json_list")
        .methods("GET"_method)([](const crow::request& req) {
            crow::json::wvalue x = crow::json::wvalue::list(messages);
            return x;
//            std::ostringstream os;
//            os << "testetsttest";
//            return crow::response{os.str()};
    });
    
    if (!isMaster)
        return;
    
    CROW_ROUTE(app, "/add_json")
      .methods("POST"_method)([](const crow::request& req) {
          auto x = crow::json::load(req.body);

          if (!x)
              return crow::response(400);
          else if (!x.has("message"))
              return crow::response(400);
          
          std::cout << "1 " << x["message"].s() << std::endl;
          sleep(10);
          std::cout << "2 " << x["message"].s() << std::endl;
          
          saveMessage(x["message"].s());
          replicateMessageRPC(x["message"].s(), id_count);
          return crow::response{201};
      });
}

int main(int argc, const char * argv[]) {
    // insert code here...
//    if (argc == 1 && argv[0] == "master")
//        isMaster = true;
    setupRoutes(true);
    
    app.port(18080).multithreaded().run();


    
    return 0;
}
