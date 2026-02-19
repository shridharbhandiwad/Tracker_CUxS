/*
 * Log Extractor and Replay Tool
 *
 * Reads binary log files produced by the tracker and can:
 * 1. Extract and print human-readable summaries
 * 2. Replay logged detections to the tracker via UDP
 * 3. Export data to CSV format
 *
 * Usage: log_extractor <logfile> [mode] [options]
 *   mode: extract (default) | replay | csv
 *   replay options: [target_ip] [target_port] [speed_factor]
 */

#include "common/types.h"
#include "common/logger.h"
#include "common/udp_socket.h"
#include "common/constants.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <map>

static std::atomic<bool> g_running{true};

void signalHandler(int) { g_running.store(false); }

const char* recordTypeName(cuas::LogRecordType type) {
    switch (type) {
        case cuas::LogRecordType::RawDetection:   return "RAW_DETECTION";
        case cuas::LogRecordType::Preprocessed:   return "PREPROCESSED";
        case cuas::LogRecordType::Clustered:      return "CLUSTERED";
        case cuas::LogRecordType::Predicted:      return "PREDICTED";
        case cuas::LogRecordType::Associated:     return "ASSOCIATED";
        case cuas::LogRecordType::TrackInitiated: return "TRACK_INIT";
        case cuas::LogRecordType::TrackUpdated:   return "TRACK_UPDATE";
        case cuas::LogRecordType::TrackDeleted:   return "TRACK_DELETE";
        case cuas::LogRecordType::TrackSent:      return "TRACK_SENT";
        default: return "UNKNOWN";
    }
}

struct LogStats {
    std::map<cuas::LogRecordType, uint64_t> counts;
    uint64_t totalRecords = 0;
    uint64_t totalBytes   = 0;
    cuas::Timestamp firstTs = 0;
    cuas::Timestamp lastTs  = 0;
};

void printExtractedRecord(const cuas::LogRecordHeader& hdr,
                           const std::vector<uint8_t>& payload) {
    auto type = static_cast<cuas::LogRecordType>(hdr.recordType);

    std::cout << "[" << std::setw(15) << hdr.timestamp << "] "
              << std::setw(16) << recordTypeName(type)
              << " (" << hdr.payloadSize << " bytes) ";

    const uint8_t* p = payload.data();

    switch (type) {
        case cuas::LogRecordType::RawDetection: {
            if (payload.size() >= 20) {
                uint32_t msgId, dwellCount, numDets;
                uint64_t ts;
                std::memcpy(&msgId, p, 4); p += 4;
                std::memcpy(&dwellCount, p, 4); p += 4;
                std::memcpy(&ts, p, 8); p += 8;
                std::memcpy(&numDets, p, 4);
                std::cout << "Dwell=" << dwellCount << " Dets=" << numDets;
            }
            break;
        }
        case cuas::LogRecordType::Preprocessed: {
            if (payload.size() >= 4) {
                uint32_t n;
                std::memcpy(&n, p, 4);
                std::cout << "FilteredDets=" << n;
            }
            break;
        }
        case cuas::LogRecordType::Clustered: {
            if (payload.size() >= 4) {
                uint32_t n;
                std::memcpy(&n, p, 4);
                std::cout << "Clusters=" << n;
            }
            break;
        }
        case cuas::LogRecordType::Predicted: {
            if (payload.size() >= 4 + cuas::STATE_DIM * 8) {
                uint32_t trackId;
                std::memcpy(&trackId, p, 4); p += 4;
                double x, y, z;
                std::memcpy(&x, p, 8);
                std::memcpy(&y, p + 24, 8);
                std::memcpy(&z, p + 48, 8);
                std::cout << "Track=" << trackId
                          << " x=" << std::fixed << std::setprecision(1) << x
                          << " y=" << y << " z=" << z;
            }
            break;
        }
        case cuas::LogRecordType::Associated: {
            if (payload.size() >= 16) {
                uint32_t trackId, clusterId;
                double dist;
                std::memcpy(&trackId, p, 4); p += 4;
                std::memcpy(&clusterId, p, 4); p += 4;
                std::memcpy(&dist, p, 8);
                std::cout << "Track=" << trackId << " Cluster=" << clusterId
                          << " Dist=" << std::fixed << std::setprecision(3) << dist;
            }
            break;
        }
        case cuas::LogRecordType::TrackInitiated: {
            if (payload.size() >= 4) {
                uint32_t trackId;
                std::memcpy(&trackId, p, 4);
                std::cout << "Track=" << trackId << " INITIATED";
            }
            break;
        }
        case cuas::LogRecordType::TrackUpdated: {
            if (payload.size() >= 8) {
                uint32_t trackId, status;
                std::memcpy(&trackId, p, 4); p += 4;
                std::memcpy(&status, p, 4);
                std::cout << "Track=" << trackId << " Status=" << status;
            }
            break;
        }
        case cuas::LogRecordType::TrackDeleted: {
            if (payload.size() >= 4) {
                uint32_t trackId;
                std::memcpy(&trackId, p, 4);
                std::cout << "Track=" << trackId << " DELETED";
            }
            break;
        }
        case cuas::LogRecordType::TrackSent: {
            if (payload.size() >= sizeof(cuas::TrackUpdateMessage)) {
                cuas::TrackUpdateMessage msg;
                std::memcpy(&msg, p, sizeof(msg));
                std::cout << "Track=" << msg.trackId
                          << " R=" << std::fixed << std::setprecision(1) << msg.range
                          << " Az=" << std::setprecision(3) << msg.azimuth * cuas::RAD2DEG
                          << " El=" << msg.elevation * cuas::RAD2DEG;
            }
            break;
        }
        default:
            break;
    }

    std::cout << std::endl;
}

int extractMode(const std::string& filename, bool verbose) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
        return 1;
    }

    LogStats stats;
    cuas::LogRecordHeader hdr;

    std::cout << "=== Log Extraction: " << filename << " ===" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    while (cuas::BinaryLogger::readHeader(file, hdr) && g_running.load()) {
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) break;

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);
        stats.counts[type]++;
        stats.totalRecords++;
        stats.totalBytes += sizeof(hdr) + hdr.payloadSize;

        if (stats.firstTs == 0) stats.firstTs = hdr.timestamp;
        stats.lastTs = hdr.timestamp;

        if (verbose) {
            printExtractedRecord(hdr, payload);
        }
    }

    std::cout << std::string(80, '-') << std::endl;
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Total records: " << stats.totalRecords << std::endl;
    std::cout << "Total bytes:   " << stats.totalBytes << std::endl;

    if (stats.firstTs > 0 && stats.lastTs > stats.firstTs) {
        double durationSec = (stats.lastTs - stats.firstTs) * 1e-6;
        std::cout << "Duration:      " << std::fixed << std::setprecision(2)
                  << durationSec << " seconds" << std::endl;
    }

    std::cout << std::endl << "Record type breakdown:" << std::endl;
    for (const auto& [type, count] : stats.counts) {
        std::cout << "  " << std::setw(16) << recordTypeName(type)
                  << ": " << count << std::endl;
    }

    return 0;
}

int replayMode(const std::string& filename, const std::string& targetIp,
                int targetPort, double speedFactor) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
        return 1;
    }

    cuas::UdpSocket::initNetwork();
    cuas::UdpSocket socket;
    socket.setDestination(targetIp, targetPort);

    std::cout << "=== Replay Mode ===" << std::endl;
    std::cout << "Target: " << targetIp << ":" << targetPort << std::endl;
    std::cout << "Speed:  " << speedFactor << "x" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    cuas::LogRecordHeader hdr;
    cuas::Timestamp prevTs = 0;
    uint64_t sentCount = 0;

    while (cuas::BinaryLogger::readHeader(file, hdr) && g_running.load()) {
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) break;

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);

        // Only replay raw detection records
        if (type == cuas::LogRecordType::RawDetection) {
            // Timing
            if (prevTs > 0 && hdr.timestamp > prevTs) {
                double delaySec = (hdr.timestamp - prevTs) * 1e-6 / speedFactor;
                if (delaySec > 0.0 && delaySec < 10.0) {
                    std::this_thread::sleep_for(
                        std::chrono::microseconds(
                            static_cast<int64_t>(delaySec * 1e6)));
                }
            }
            prevTs = hdr.timestamp;

            // Reconstruct SPDetectionMessage from payload
            if (payload.size() >= 20) {
                cuas::SPDetectionMessage msg;
                const uint8_t* p = payload.data();
                std::memcpy(&msg.messageId, p, 4); p += 4;
                std::memcpy(&msg.dwellCount, p, 4); p += 4;
                std::memcpy(&msg.timestamp, p, 8); p += 8;
                std::memcpy(&msg.numDetections, p, 4); p += 4;

                // Update timestamp to now
                msg.timestamp = cuas::nowMicros();

                msg.detections.resize(msg.numDetections);
                for (uint32_t i = 0; i < msg.numDetections; ++i) {
                    std::memcpy(&msg.detections[i], p, sizeof(cuas::Detection));
                    p += sizeof(cuas::Detection);
                }

                auto data = cuas::MessageSerializer::serialize(msg);
                socket.send(data.data(), static_cast<int>(data.size()));
                ++sentCount;

                if (sentCount % 50 == 0) {
                    std::cout << "Replayed " << sentCount << " dwells (dwell "
                              << msg.dwellCount << ", " << msg.numDetections
                              << " dets)" << std::endl;
                }
            }
        }
    }

    std::cout << "Replay complete. Sent " << sentCount << " detection messages." << std::endl;
    cuas::UdpSocket::cleanupNetwork();
    return 0;
}

int csvMode(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
        return 1;
    }

    cuas::LogRecordHeader hdr;

    // CSV header for track sent records
    std::cout << "timestamp,record_type,track_id,range,azimuth_deg,elevation_deg,"
              << "range_rate,x,y,z,vx,vy,vz,quality,hits,misses,age,status,class"
              << std::endl;

    while (cuas::BinaryLogger::readHeader(file, hdr) && g_running.load()) {
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) break;

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);

        if (type == cuas::LogRecordType::TrackSent &&
            payload.size() >= sizeof(cuas::TrackUpdateMessage)) {
            cuas::TrackUpdateMessage msg;
            std::memcpy(&msg, payload.data(), sizeof(msg));

            std::cout << hdr.timestamp << ","
                      << recordTypeName(type) << ","
                      << msg.trackId << ","
                      << std::fixed << std::setprecision(2)
                      << msg.range << ","
                      << msg.azimuth * cuas::RAD2DEG << ","
                      << msg.elevation * cuas::RAD2DEG << ","
                      << msg.rangeRate << ","
                      << msg.x << "," << msg.y << "," << msg.z << ","
                      << msg.vx << "," << msg.vy << "," << msg.vz << ","
                      << msg.trackQuality << ","
                      << msg.hitCount << ","
                      << msg.missCount << ","
                      << msg.age << ","
                      << static_cast<int>(msg.status) << ","
                      << static_cast<int>(msg.classification)
                      << std::endl;
        }
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Counter-UAS Radar Tracker - Log Extractor & Replay Tool" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Usage: " << argv[0] << " <logfile> [mode] [options]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  extract [verbose]              - Extract and print log contents" << std::endl;
        std::cerr << "  replay [ip] [port] [speed]     - Replay detections via UDP" << std::endl;
        std::cerr << "  csv                            - Export track data as CSV" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_20260101_120000.bin extract verbose" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_20260101_120000.bin replay 127.0.0.1 50000 2.0" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_20260101_120000.bin csv > tracks.csv" << std::endl;
        return 1;
    }

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

    std::string filename = argv[1];
    std::string mode     = (argc > 2) ? argv[2] : "extract";

    if (mode == "extract") {
        bool verbose = (argc > 3 && std::string(argv[3]) == "verbose");
        return extractMode(filename, verbose);
    } else if (mode == "replay") {
        std::string targetIp = (argc > 3) ? argv[3] : "127.0.0.1";
        int targetPort       = (argc > 4) ? std::stoi(argv[4]) : 50000;
        double speedFactor   = (argc > 5) ? std::stod(argv[5]) : 1.0;
        return replayMode(filename, targetIp, targetPort, speedFactor);
    } else if (mode == "csv") {
        return csvMode(filename);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
}
