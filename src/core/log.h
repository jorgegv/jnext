#pragma once

/// Logging subsystem — thin wrapper around spdlog.
///
/// Usage:
///   #include "core/log.h"
///   Log::cpu()->info("PC={:#06x} opcode={:#04x}", pc, op);
///   Log::video()->trace("vc={} hc={}", vc, hc);
///
/// Each subsystem has a dedicated named logger whose level can be
/// changed independently at runtime via Log::set_level().

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class Log {
public:
    // Per-subsystem loggers.
    static std::shared_ptr<spdlog::logger>& cpu()        { static auto l = make("cpu");        return l; }
    static std::shared_ptr<spdlog::logger>& memory()     { static auto l = make("memory");     return l; }
    static std::shared_ptr<spdlog::logger>& ula()        { static auto l = make("ula");        return l; }
    static std::shared_ptr<spdlog::logger>& video()      { static auto l = make("video");      return l; }
    static std::shared_ptr<spdlog::logger>& audio()      { static auto l = make("audio");      return l; }
    static std::shared_ptr<spdlog::logger>& port()       { static auto l = make("port");       return l; }
    static std::shared_ptr<spdlog::logger>& nextreg()    { static auto l = make("nextreg");    return l; }
    static std::shared_ptr<spdlog::logger>& dma()        { static auto l = make("dma");        return l; }
    static std::shared_ptr<spdlog::logger>& copper()     { static auto l = make("copper");     return l; }
    static std::shared_ptr<spdlog::logger>& uart()       { static auto l = make("uart");       return l; }
    static std::shared_ptr<spdlog::logger>& input()      { static auto l = make("input");      return l; }
    static std::shared_ptr<spdlog::logger>& platform()   { static auto l = make("platform");   return l; }
    static std::shared_ptr<spdlog::logger>& emulator()   { static auto l = make("emulator");   return l; }

    /// Set the log level for a specific subsystem by name.
    /// Returns false if the logger name is unknown.
    static bool set_level(const std::string& name, spdlog::level::level_enum level) {
        auto logger = spdlog::get(name);
        if (!logger) return false;
        logger->set_level(level);
        return true;
    }

    /// Set the default log level for all subsystems.
    static void set_default_level(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    /// Parse a log-level spec string: "cpu=trace,video=warn,port=debug"
    /// Unrecognised logger names are silently ignored.
    static void parse_levels(const std::string& spec) {
        std::string::size_type pos = 0;
        while (pos < spec.size()) {
            auto comma = spec.find(',', pos);
            auto token = spec.substr(pos, comma == std::string::npos ? comma : comma - pos);
            pos = (comma == std::string::npos) ? spec.size() : comma + 1;

            auto eq = token.find('=');
            if (eq == std::string::npos) continue;
            auto name  = token.substr(0, eq);
            auto lstr  = token.substr(eq + 1);
            auto level = spdlog::level::from_str(lstr);
            set_level(name, level);
        }
    }

    /// Force-create all loggers (call once at startup before parse_levels).
    static void init() {
        cpu(); memory(); ula(); video(); audio(); port(); nextreg();
        dma(); copper(); uart(); input(); platform(); emulator();
    }

private:
    static std::shared_ptr<spdlog::logger> make(const char* name) {
        auto existing = spdlog::get(name);
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt(name);
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }
};
