#pragma once

#include <vector>

struct SParameters
{
    bool isMaster;
    std::string hostname;
    uint32_t http_port;
    std::string rpc_port;
    std::vector<std::string> slaves;
};

bool parse_args(int argc, const char** argv, struct SParameters *params);