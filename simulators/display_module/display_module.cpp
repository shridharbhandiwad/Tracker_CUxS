/*
 * Display Module Simulator
 *
 * Receives track update messages from the tracker over UDP
 * and displays them in a formatted console output.
 *
 * Usage: display_module [listen_port]
 */

#include "common/types.h"
#include "common/udp_socket.h"
#include "common/constants.h"
#include "common/logger.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cmath>

static std::atomic<bool> g_running{true};

void signalHandler(int) { g_running.store(false); }

const char* statusToString(cuas::TrackStatus s) {
    switch (s) {
        case cuas::TrackStatus::Tentative:  return "TENT";
        case cuas::TrackStatus::Confirmed:  return "CONF";
        case cuas::TrackStatus::Coasting:   return "COAST";
        case cuas::TrackStatus::Deleted:    return "DEL";
        default: return "???";
    }
}

const char* classToString(cuas::TrackClassification c) {
    switch (c) {
        case cuas::TrackClassification::Unknown:        return "UNKNOWN";
        case cuas::TrackClassification::DroneRotary:    return "DRONE-R";
        case cuas::TrackClassification::DroneFixedWing: return "DRONE-F";
        case cuas::TrackClassification::Bird:           return "BIRD";
        case cuas::TrackClassification::Clutter:        return "CLUTTER";
        default: return "???";
    }
}

void printHeader() {
    std::cerr <<
        "================================================================\n"
        "  Display Module - Track Viewer\n"
        "================================================================\n\n";
}

void printTrackTable(const std::vector<cuas::TrackUpdateMessage>& tracks) {
    // Clear screen effect
    std::cout << "\033[2J\033[H";

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::cout << "=== COUNTER-UAS RADAR TRACKER DISPLAY ===" << std::endl;
    std::cout << "Time: " << std::put_time(&tm, "%H:%M:%S") << "  Tracks: " << tracks.size() << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    std::cout << std::setw(5)  << "ID"
              << std::setw(8)  << "Status"
              << std::setw(10) << "Class"
              << std::setw(10) << "Range(m)"
              << std::setw(10) << "Az(deg)"
              << std::setw(10) << "El(deg)"
              << std::setw(10) << "Rdot(m/s)"
              << std::setw(10) << "X(m)"
              << std::setw(10) << "Y(m)"
              << std::setw(10) << "Z(m)"
              << std::setw(8)  << "Quality"
              << std::setw(6)  << "Hits"
              << std::setw(6)  << "Miss"
              << std::setw(6)  << "Age"
              << std::endl;

    std::cout << std::string(120, '-') << std::endl;

    for (const auto& t : tracks) {
        double azDeg = t.azimuth * cuas::RAD2DEG;
        double elDeg = t.elevation * cuas::RAD2DEG;

        std::cout << std::setw(5)  << t.trackId
                  << std::setw(8)  << statusToString(t.status)
                  << std::setw(10) << classToString(t.classification)
                  << std::setw(10) << std::fixed << std::setprecision(1) << t.range
                  << std::setw(10) << std::setprecision(2) << azDeg
                  << std::setw(10) << std::setprecision(2) << elDeg
                  << std::setw(10) << std::setprecision(1) << t.rangeRate
                  << std::setw(10) << std::setprecision(1) << t.x
                  << std::setw(10) << std::setprecision(1) << t.y
                  << std::setw(10) << std::setprecision(1) << t.z
                  << std::setw(8)  << std::setprecision(2) << t.trackQuality
                  << std::setw(6)  << t.hitCount
                  << std::setw(6)  << t.missCount
                  << std::setw(6)  << t.age
                  << std::endl;
    }

    int confirmed = 0, tentative = 0, coasting = 0;
    for (const auto& t : tracks) {
        if (t.status == cuas::TrackStatus::Confirmed) ++confirmed;
        else if (t.status == cuas::TrackStatus::Tentative) ++tentative;
        else if (t.status == cuas::TrackStatus::Coasting) ++coasting;
    }

    std::cout << std::string(120, '-') << std::endl;
    std::cout << "Summary: " << confirmed << " confirmed, "
              << tentative << " tentative, "
              << coasting << " coasting" << std::endl;
    std::cout << std::flush;
}

int main(int argc, char* argv[]) {
    int listenPort = 50001;
    if (argc > 1) listenPort = std::stoi(argv[1]);

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

    cuas::ConsoleLogger::instance().setLevel(cuas::ConsoleLogger::INFO);
    printHeader();

    cuas::UdpSocket::initNetwork();
    cuas::UdpSocket socket;

    if (!socket.bindSocket("0.0.0.0", listenPort)) {
        LOG_ERROR("Display", "Failed to bind on port %d", listenPort);
        return 1;
    }
    socket.setReceiveTimeout(500);

    LOG_INFO("Display", "Listening for track updates on port %d", listenPort);

    std::vector<uint8_t> buffer(65536);
    uint64_t msgCount = 0;

    while (g_running.load()) {
        int n = socket.receive(buffer.data(), static_cast<int>(buffer.size()));
        if (n <= 0) continue;

        // Try track table first
        std::vector<cuas::TrackUpdateMessage> tracks;
        uint64_t timestamp;

        if (cuas::MessageSerializer::deserializeTrackTable(
                buffer.data(), n, tracks, timestamp)) {
            ++msgCount;
            printTrackTable(tracks);
        } else {
            // Try single track update
            cuas::TrackUpdateMessage single;
            if (cuas::MessageSerializer::deserialize(buffer.data(), n, single)) {
                ++msgCount;
                std::vector<cuas::TrackUpdateMessage> singles = {single};
                printTrackTable(singles);
            }
        }
    }

    LOG_INFO("Display", "Exiting. Total messages received: %lu",
             static_cast<unsigned long>(msgCount));
    cuas::UdpSocket::cleanupNetwork();
    return 0;
}
