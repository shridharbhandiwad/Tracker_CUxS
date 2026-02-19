#include "common/config.h"
#include "common/logger.h"
#include "pipeline/tracker_pipeline.h"

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    (void)sig;
    g_running.store(false);
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
