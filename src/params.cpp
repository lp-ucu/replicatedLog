#include "tclap/CmdLine.h"
#include <iostream>
#include "params.hpp"
// using TCLAP: https://tclap.sourceforge.net/manual.html

#define REPLICATED_LOG_VERSION "1.0"
// master:
// .\build\src\Debug\main.exe -m --hostname 127.0.0.1 --grpc-port 50050 --http-port 18080 -S 127.0.0.1:50051 -S 127.0.0.1:50052

// slave:
// .\build\src\Debug\main.exe -s --grpc-port 50051 --hostname 127.0.0.1 --http-port 28080

bool parse_args(int argc, const char **argv, struct SParameters *params)
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
        std::cerr << "Error: " << e.error() << std::endl;
        // usage()
        return false;
    }
}