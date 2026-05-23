#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <ctime>

enum class LogLevel { DEBUG, INFO, WARN, ERR };

class Logger
{
public:
    Logger(const std::string &filepath, LogLevel min_level = LogLevel::DEBUG)
        : min_level(min_level), file(filepath, std::ios::app)
    {
        if (!file)
            throw std::runtime_error("Logger: cannot open log file: " + filepath);
    }

    void debug(const std::string &msg) { log(LogLevel::DEBUG, msg); }
    void info (const std::string &msg) { log(LogLevel::INFO,  msg); }
    void warn (const std::string &msg) { log(LogLevel::WARN,  msg); }
    void error(const std::string &msg) { log(LogLevel::ERR,   msg); }

private:
    LogLevel      min_level;
    std::ofstream file;

    void log(LogLevel level, const std::string &msg)
    {
        if (level < min_level) return;

        std::string entry = timestamp() + " [" + level_str(level) + "] " + msg + "\n";
        file  << entry;
        file.flush();
        std::cerr << entry;
    }

    static std::string level_str(LogLevel l)
    {
        switch (l)
        {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERR:   return "ERROR";
        }
        return "?????";
    }

    static std::string timestamp()
    {
        std::time_t t = std::time(nullptr);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        return buf;
    }
};
