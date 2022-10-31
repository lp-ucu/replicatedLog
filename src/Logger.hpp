#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include "crow_all.h"

class Logger {
    public:
    
    explicit Logger(const std::string& filename = ""):
        _cerrbuf{ std::cerr.rdbuf() },
        _coutbuf{ std::cout.rdbuf() },
        _outf{ filename },
        _fname{ filename }
    {
        if (!filename.empty())
        // replace cout's rdbuf with the file's rdbuf
        {
            std::cout.rdbuf(_outf.rdbuf());
            std::cerr.rdbuf(_outf.rdbuf());
            //for some reason grpc logs cannot be redirected by std::cerr
            freopen(_fname.c_str(), "a+", stderr);
        }
    }
    
    ~Logger() {
        // restore cout's rdbuf to the original
        std::cout << std::flush;
        std::cerr << std::flush;
        std::cout.rdbuf(_coutbuf);
        std::cout.rdbuf(_cerrbuf);
        fclose (stderr);
    }
    
    private:
    std::streambuf* _coutbuf;
    std::streambuf* _cerrbuf;
    std::ofstream _outf;
    std::string _fname;
};

//currently using crow logger
//TODO: rework debug define to have correct prefix
#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define LOG_DEBUG                                                  \
    crow::logger(crow::LogLevel::Info)
#else
#define LOG_DEBUG
#endif

#define LOG_CRITICAL                                                  \
    if (crow::logger::get_current_log_level() <= crow::LogLevel::Critical) \
    crow::logger(crow::LogLevel::Critical)
#define LOG_ERROR                                                  \
    if (crow::logger::get_current_log_level() <= crow::LogLevel::Error) \
    crow::logger(crow::LogLevel::Error)
#define LOG_WARNING                                                  \
    if (crow::logger::get_current_log_level() <= crow::LogLevel::Warning) \
    crow::logger(crow::LogLevel::Warning)
#define LOG_INFO                                                  \
    if (crow::logger::get_current_log_level() <= crow::LogLevel::Info) \
    crow::logger(crow::LogLevel::Info)

