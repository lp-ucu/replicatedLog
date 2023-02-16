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
  std::string hostname_;
  grpc::ClientContext context_;
  Status status_;
  bool finished_ = false;

  GreetRequest(const size_t id, const std::string &text, std::string server)
      : hostname_(server),
        stub_(ReplicateService::NewStub(grpc::CreateChannel(server, grpc::InsecureChannelCredentials())))
  {
    request_.set_id(id);
    request_.set_text(text);
  }

  bool send(std::mutex *mu, std::condition_variable *cv, uint32_t waitfor)
  {
    finished_ = false;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::seconds(waitfor);
    context_.set_deadline(deadline);
  
    LOG_DEBUG << "  Sending request to " << hostname_<<"...";
    stub_->async()->appendMessage(&context_, &request_, &reply_,
                                  [mu, cv, this](Status s)
                                  {
                                    std::lock_guard<std::mutex> lock(*mu);
                                    status_ = s;
                                    finished_ = true;
                                    // LOG_DEBUG << "  --- Request finished: "<<status_.error_message();
                                    cv->notify_one();
                                  });
    return true;
  }

  void cancel()
  {
    context_.TryCancel();
  }

  const int32_t res()
  {
    return reply_.res();
  }

private:
  std::unique_ptr<ReplicateService::Stub> stub_;
  MessageItem request_;
  ReplicateResponce reply_;

};

bool SyncMessage(int32_t id, const std::string &msg, std::string server)
{
  std::mutex r_mutex;
  std::condition_variable request_cv;
  auto request = GreetRequest(id, msg, server);
  request.send(&r_mutex, &request_cv, 5);
  LOG_DEBUG << " Syncing message [" << id << ". \'" << msg << "\'] with "<<server<<";";

  while(1)
  {
    std::unique_lock<std::mutex> lock(r_mutex);
    request_cv.wait_for(lock, std::chrono::seconds(5));
    if(request.finished_) break;
  }
  LOG_INFO << "   Message sync result: " << (request.status_.ok() ? "OK" : request.status_.error_message());

  return request.status_.ok();
}


bool ReplicateMessage(int32_t id, const std::string &msg, const std::vector<std::string> &servers, uint32_t write_concern)
{
  std::mutex r_mutex;
  uint32_t success_count = 0;
  uint32_t fail_count = 0;

  std::condition_variable request_cv;

  std::vector<GreetRequest *> requests{};

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    requests.push_back(new GreetRequest(id, msg, servers[i]));
    requests[i]->send(&r_mutex, &request_cv, 5);
  }

  {
    while ((success_count < write_concern) && HealthMonitor::getInstance().isRunning())
    {
      std::unique_lock<std::mutex> lock(r_mutex);
      request_cv.wait_for(lock, std::chrono::seconds(10));
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
            LOG_DEBUG << "    Request to " << requests[i]->hostname_ << " success";
          }
          else
          {
            fail_count++;
            LOG_DEBUG << "    Request to " << requests[i]->hostname_ << " error:" << requests[i]->status_.error_message();
            // Re-send request: delete old GreetRequest object, create a new one and send request to the same server
            delete requests[i];
            requests[i] = new GreetRequest(id, msg, servers[i]);
            requests[i]->send(&r_mutex, &request_cv, 2);
          }
        }
      }
    }
  }

  bool all_finished = true;
  for(auto request:requests)
  {
    if (!request->finished_)
    {
      request->cancel();
      all_finished = false;
    }
  }

  // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  while (!all_finished)
  {
    std::unique_lock<std::mutex> lock(r_mutex);

    all_finished = true;
    for(auto request:requests)
    {
      if (!request->finished_) all_finished = false;
    }
    if (all_finished) break;

    // LOG_INFO << " Wait for requests to finish:";
    request_cv.wait_for(lock, std::chrono::seconds(5));
    // LOG_INFO << " ...";
  }

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    if (requests[i] != nullptr)
    {
      // LOG_INFO << "  - Delete:";
      delete requests[i];
      requests[i] = nullptr;
    }
  }
  LOG_INFO << " Message replicate result:" << ((success_count >= write_concern) ? "OK" : "KO");

  return success_count >= write_concern;
}
