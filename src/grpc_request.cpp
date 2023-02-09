#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"
#include "Logger.hpp"
#include "HealthMonitor.h"
// #include "myproto/hello.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

using replicatedlog::MessageItem;
using replicatedlog::ReplicateResponce;
using replicatedlog::ReplicateService;

class GreetRequest
{
public:
  std::string server;
  grpc::ClientContext context;
  // bool ok = 0;
  Status status_;
  bool finished_ = false;

  GreetRequest(const size_t id, const std::string &text, std::string server)
      : server(server),
        stub_(ReplicateService::NewStub(grpc::CreateChannel(server, grpc::InsecureChannelCredentials())))
  {
    request.set_id(id);
    request.set_text(text);
  }

  bool send(std::mutex *mu, std::condition_variable *cv, uint32_t waitfor)
  {
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::seconds(waitfor);
    context.set_deadline(deadline);
    finished_ = false;
    LOG_DEBUG << "  Sending request to " << server<<"...";
    stub_->async()->appendMessage(&context, &request, &reply,
                                  [mu, cv, this](Status s)
                                  {
                                    std::lock_guard<std::mutex> lock(*mu);
                                    status_ = s;
                                    finished_ = true;
                                    cv->notify_one();
                                  });
    return true;
  }

  const int32_t res()
  {
    return reply.res();
  }

private:
  std::unique_ptr<ReplicateService::Stub> stub_;
  MessageItem request;
  ReplicateResponce reply;

};

bool ReplicateMessage(int32_t id, const std::string &msg, const std::vector<std::string> &servers, uint32_t write_concern)
{
  std::mutex r_mutex;
  uint32_t success_count = 0;
  uint32_t fail_count = 0;

  std::condition_variable g_cv;

  std::vector<GreetRequest *> requests{};

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    requests.push_back(new GreetRequest(id, msg, servers[i]));
    requests[i]->send(&r_mutex, &g_cv, 2);
  }

  {
    std::unique_lock<std::mutex> lock(r_mutex);
    while ((success_count < write_concern) && HealthMonitor::getInstance().isRunning())
    {
      g_cv.wait_for(lock, std::chrono::seconds(5));
      LOG_DEBUG << "  Replicating message [" << id << ". \'" << msg << "\'];";
      success_count = 0;
      fail_count = 0;
      for (uint32_t i = 0; i < servers.size(); i++)
      {
        if (requests[i]->finished_)
        {
          if (requests[i]->status_.ok())
          {
            success_count++;
            LOG_DEBUG << "    Request " << i << " success";
          }
          else
          {
            LOG_DEBUG << "    Request " << i << " error:" << requests[i]->status_.error_message();
            // Re-send request: delete old GreetRequest object, create a new one and send request to the same server
            delete requests[i];
            requests[i] = new GreetRequest(id, msg, servers[i]);
            requests[i]->send(&r_mutex, &g_cv, 2);
          }
        }
      }
    }
  }

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    // LOG_INFO << "    Request " << i << " returned " << requests[i]->res();
    // TODO: TryCancel may not work
    requests[i]->context.TryCancel();
    delete requests[i];
  }

  LOG_INFO << " Message replicate result:" << ((success_count >= write_concern) ? "OK" : "KO");
  return success_count >= write_concern;
}
