#include "common/config.h"
#include "common/logger.h"
#include "pipeline/tracker_pipeline.h"

#include <iostream>
#include <fstream>
#include <string>
#include <csignal>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    (void)sig;
    g_running.store(false);
}

static std::string getExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::string path(buf, len);
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    std::string path(buf, static_cast<size_t>(len));
#endif
    auto pos = path.find_last_of("/\\");
    return (pos != std::string::npos) ? path.substr(0, pos) : std::string{};
}

static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

static std::string resolveConfigPath(const std::string& configPath) {
    if (fileExists(configPath))
        return configPath;

    std::string exeDir = getExecutableDir();
    if (exeDir.empty())
        return configPath;

    // Try next to the executable (handles post-build copy)
    std::string candidate = exeDir + "/" + configPath;
    if (fileExists(candidate))
        return candidate;

    // Try one level up (e.g. build/Debug -> build/)
    auto pos = exeDir.find_last_of("/\\");
    if (pos != std::string::npos) {
        std::string parent = exeDir.substr(0, pos);
        candidate = parent + "/" + configPath;
        if (fileExists(candidate))
            return candidate;

        // Try two levels up (e.g. build/Debug -> project root)
        pos = parent.find_last_of("/\\");
        if (pos != std::string::npos) {
            candidate = parent.substr(0, pos) + "/" + configPath;
            if (fileExists(candidate))
                return candidate;
        }
    }

    return configPath;
}

void printBanner() {
    std::cerr <<
        "================================================================\n"
        "  Counter-UAS Radar Tracker v1.0\n"
        "  3D Object Tracker for Defence Radar System\n"
        "================================================================\n";
}

int main(int argc, char* argv[]) {
    printBanner();

    std::string configPath = "config/tracker_config.json";
    if (argc > 1) {
        configPath = argv[1];
    }

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

    try {
        cuas::ConsoleLogger::instance().setLevel(cuas::ConsoleLogger::DEBUG);

        configPath = resolveConfigPath(configPath);
        LOG_INFO("Main", "Loading configuration from: %s", configPath.c_str());
        cuas::TrackerConfig config = cuas::loadConfig(configPath);

        cuas::ConsoleLogger::instance().setLevel(
            static_cast<cuas::ConsoleLogger::Level>(config.system.logLevel));

        cuas::TrackerPipeline pipeline(config);

        if (!pipeline.start()) {
            LOG_ERROR("Main", "Failed to start tracker pipeline");
            return 1;
        }

        LOG_INFO("Main", "Tracker running. Press Ctrl+C to stop.");

        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        LOG_INFO("Main", "Shutting down...");
        pipeline.stop();

    } catch (const std::exception& e) {
        LOG_ERROR("Main", "Fatal error: %s", e.what());
        return 1;
    }

    LOG_INFO("Main", "Tracker exited cleanly");
    cuas::UdpSocket::cleanupNetwork();
    return 0;
}
