#include <iostream>
#include <mutex>
#include <string>
#include <vector>


bool ReplicateMessage(int32_t id, const std::string& msg, const std::vector<std::string> &servers, uint32_t write_concern);
