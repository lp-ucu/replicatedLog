#include "tclap/CmdLine.h"
#include <iostream>
#include "src/params.hpp"
// using TCLAP: https://tclap.sourceforge.net/manual.html

#define REPLICATED_LOG_VERSION "0.9"
// master:
// ./main -m --hostname "$RL_HOSTNAME" --http-port "18080" --slave-hostname -S host1:2222 -S host2:3333"
// slave:
// ./main -s --hostname "$RL_HOSTNAME" --http-port "28080" --grpc-port "$RPC_PORT"


// struct SParameters
// {
//     bool isMaster;
//     std::string hostname;
//     uint32_t http_port;
//     std::vector<std::string> slaves;
// } params = {0};

bool parse_args(int argc, char **argv, struct SParameters *params)
{
    // Wrap everything in a try block.  Do this every time,
    // because exceptions will be thrown for problems.
    try
    {
        // Define the command line object.
        TCLAP::CmdLine cmd("A log replication server", ' ', REPLICATED_LOG_VERSION);

        TCLAP::SwitchArg argIsMaster("m", "master", "Launch as master", false);
        TCLAP::SwitchArg argIsSlave("s", "slave", "Launch as secondary", false);
        TCLAP::OneOf OneOfMode;
        OneOfMode.add(argIsMaster).add(argIsSlave);
        cmd.add(OneOfMode);

        TCLAP::ValueArg<std::string> argHostname("", "hostname", "Hostname to bind to", true, "", "hostname");
        cmd.add(argHostname);

        TCLAP::ValueArg<uint32_t> argHttpPort("", "http-port", "Port to bind to for HTTP", true, 0, "port");
        cmd.add(argHttpPort);

        TCLAP::ValueArg<std::string> argRPCPort("", "grpc-port", "Port to bind to for RPC", false, "", "port");
        cmd.add(argRPCPort);

        TCLAP::MultiArg<std::string> argSlaves("S", "add_slave", "list of secondaries in hostname:port fomat", false, "hosname:port");
        cmd.add(argSlaves);

        // // Parse the args.
        cmd.parse(argc, argv);

        bool isMaster = argIsMaster.getValue();
        bool isSlave = argIsSlave.getValue();

        params->isMaster = argIsMaster.getValue();;

        if (params->isMaster && !argSlaves.isSet())
        {

            throw(TCLAP::CmdLineParseException("At least one slave must be specified!"));
            // cmd._output->failure(cmd, TCLAP::CmdLineParseException("At least one secondary required!"));
        }

        if (!params->isMaster && !argRPCPort.isSet())
        {

            throw(TCLAP::CmdLineParseException("grpc-port must be specified for slave!"));
        }

        params->hostname = argHostname.getValue();
        params->http_port = argHttpPort.getValue();
        if (argRPCPort.isSet())
        {
            params->rpc_port = argRPCPort.getValue();
        }
        params->slaves = argSlaves.getValue();

        return true;
    }
    catch (TCLAP::ArgException &e) // catch any exceptions
    {
        // std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        std::cerr << "Error: " << e.error() << std::endl;
        // usage()
        
        return false;
    }
    // return 0;
}

// int main(int argc, char **argv)
// {
//     SParameters params = {
//         .isMaster=true,
//         .hostname="",
//         .http_port=80,
//         .rpc_port="1234",
//         .slaves={}
//     };
//     if (parse_args(argc, argv, &params))
//     {

//         std::cout << "Running as " << (params.isMaster ? "master" : "slave") << "; \"" << params.hostname << "\":" << params.http_port << ":" << params.rpc_port << std::endl;
//         std::cout << "Secondaries:" << std::endl;
//         for (std::string &slave : params.slaves)
//         {
//             std::cout << slave << std::endl;
//         }
//         return 0;
//     }
//     return -1;
// }
