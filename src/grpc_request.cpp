#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "replicate.grpc.pb.h"

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

  GreetRequest(const size_t id, const std::string &text, std::string server)
      : server(server),
        stub_(ReplicateService::NewStub(grpc::CreateChannel(server, grpc::InsecureChannelCredentials())))
  {
    request.set_id(id);
    request.set_text(text);
  }

  bool send(std::mutex *mu, std::condition_variable *cv, uint32_t *fail_count, uint32_t *success_count, uint32_t waitfor)
  {
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::seconds(waitfor);
    context.set_deadline(deadline);

    std::cout << "  Request to " << server;
    stub_->async()->appendMessage(&context, &request, &reply,
                                  [mu, cv, fail_count, success_count](Status s)
                                  {
                                    // std::cout << "    r\r\n";
                                    std::lock_guard<std::mutex> lock(*mu);
                                    //  done = true;
                                    if (s.ok())
                                    {
                                      (*success_count)++;
                                    }
                                    else
                                    {
                                      std::cout << s.error_message() << std::endl;
                                      (*fail_count)++;
                                    }
                                    cv->notify_one();
                                  });
    std::cout << " sent" << std::endl;
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
  std::mutex g_mu;
  uint32_t success_count = 0;
  uint32_t fail_count = 0;

  std::condition_variable g_cv;

  std::vector<GreetRequest *> requests{};

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    GreetRequest *request = new GreetRequest(id, msg, servers[i]);
    requests.push_back(request);
    request->send(&g_mu, &g_cv, &fail_count, &success_count, 2);
  }

  {
    std::unique_lock<std::mutex> lock(g_mu);
    while ((success_count < write_concern) && (success_count + fail_count < servers.size()))
    {
      std::cout << "   wait " << success_count << " " << fail_count << std::endl;
      g_cv.wait(lock);
    }
  }

  std::cout << "  Wait finished" << std::endl;

  for (uint32_t i = 0; i < servers.size(); i++)
  {
    std::cout << " res "<<i<<" :" << requests[i]->res() << std::endl;
    // TODO: TryCancel may not work
    requests[i]->context.TryCancel();
    delete requests[i];
  }
  std::cout << " Result:"<< (success_count >= write_concern) << std::endl;
  return success_count >= write_concern;
}