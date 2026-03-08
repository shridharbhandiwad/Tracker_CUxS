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
#include "common/constants.h"
#include "common/dds_participant.h"

#include <fastdds/dds/publisher/DataWriter.hpp>

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <map>
#include <filesystem>

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
        case cuas::LogRecordType::RunInfo:        return "RUN_INFO";
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
            // Layout: msgId(4) trkId(4) ts(8) stat(4) cls(4) range(8) az(8) el(8)
            //         rr(8) x(8) y(8) z(8) vx(8) vy(8) vz(8) qual(8) hits(4) miss(4) age(4)
            if (payload.size() >= 24) {
                uint32_t trkId; uint32_t stat;
                double range, az, el;
                p += 4; // skip msgId
                std::memcpy(&trkId, p, 4); p += 4;
                p += 8; // skip ts
                std::memcpy(&stat, p, 4); p += 4;
                p += 4; // skip cls
                std::memcpy(&range, p, 8); p += 8;
                std::memcpy(&az,    p, 8); p += 8;
                std::memcpy(&el,    p, 8);
                std::cout << "Track=" << trkId
                          << " R=" << std::fixed << std::setprecision(1) << range
                          << " Az=" << std::setprecision(3) << az * cuas::RAD2DEG
                          << " El=" << el * cuas::RAD2DEG;
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

    uint64_t corruptedRecords = 0;
    while (g_running.load()) {
        if (!cuas::BinaryLogger::readHeader(file, hdr)) {
            if (file.eof()) break;
            // Bad SOM or I/O error — attempt to resync to the next record.
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            ++corruptedRecords;
            continue;
        }

        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) {
            // Missing or wrong EOM — attempt to resync and continue.
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            ++corruptedRecords;
            continue;
        }

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);
        stats.counts[type]++;
        stats.totalRecords++;
        // Each record on disk: header + payload + EOM (4 bytes)
        stats.totalBytes += sizeof(hdr) + hdr.payloadSize + sizeof(uint32_t);

        if (stats.firstTs == 0) stats.firstTs = hdr.timestamp;
        stats.lastTs = hdr.timestamp;

        if (verbose) {
            printExtractedRecord(hdr, payload);
        }
    }
    if (corruptedRecords > 0) {
        std::cerr << "WARNING: " << corruptedRecords
                  << " corrupted/truncated record(s) skipped.\n";
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

int replayMode(const std::string& filename, const std::string& /*targetIp*/,
               int /*targetPort*/, double speedFactor) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
        return 1;
    }

    // Publish replayed detections on the DDS "SPDetection" topic.
    cuas::CuasDdsParticipant participant;
    auto* writer = participant.makeWriter<CounterUAS::SPDetectionMessage>(
                       cuas::TOPIC_SP_DETECTION);

    std::cout << "=== Replay Mode ===" << std::endl;
    std::cout << "DDS topic: " << cuas::TOPIC_SP_DETECTION << "  domain: 0" << std::endl;
    std::cout << "Speed:  " << speedFactor << "x" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    cuas::LogRecordHeader hdr;
    cuas::Timestamp prevTs = 0;
    uint64_t sentCount = 0;

    while (g_running.load()) {
        if (!cuas::BinaryLogger::readHeader(file, hdr)) {
            if (file.eof()) break;
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            continue;
        }
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) {
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            continue;
        }

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

                // Reconstruct SPDetectionMessage from payload and publish via DDS.
            if (payload.size() >= 20) {
                const uint8_t* p = payload.data();
                uint32_t msgId, dwellCount, numDets; uint64_t ts;
                std::memcpy(&msgId,      p, 4); p += 4;
                std::memcpy(&dwellCount, p, 4); p += 4;
                std::memcpy(&ts,         p, 8); p += 8;
                std::memcpy(&numDets,    p, 4); p += 4;

                CounterUAS::SPDetectionMessage idlMsg;
                idlMsg.messageId(msgId);
                idlMsg.dwellCount(dwellCount);
                idlMsg.timestamp(cuas::nowMicros()); // update to current time
                idlMsg.numDetections(numDets);

                std::vector<CounterUAS::DetectionData> dets;
                dets.reserve(numDets);
                for (uint32_t i = 0; i < numDets; ++i) {
                    if (p + sizeof(cuas::Detection) > payload.data() + payload.size()) break;
                    cuas::Detection d;
                    std::memcpy(&d, p, sizeof(d)); p += sizeof(d);
                    dets.push_back(cuas::toIDL(d));
                }
                idlMsg.detections(dets);

                writer->write(&idlMsg);
                ++sentCount;

                if (sentCount % 50 == 0) {
                    std::cout << "Replayed " << sentCount << " dwells (dwell "
                              << dwellCount << ", " << numDets
                              << " dets)" << std::endl;
                }
            }
        }
    }

    std::cout << "Replay complete. Published " << sentCount << " detection messages on DDS." << std::endl;
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

    while (g_running.load()) {
        if (!cuas::BinaryLogger::readHeader(file, hdr)) {
            if (file.eof()) break;
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            continue;
        }
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) {
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            continue;
        }

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);

        if (type == cuas::LogRecordType::TrackSent && payload.size() >= 108u) {
            // Layout: msgId(4) trkId(4) ts(8) stat(4) cls(4) range(8) az(8) el(8)
            //         rr(8) x(8) y(8) z(8) vx(8) vy(8) vz(8) qual(8) hits(4) miss(4) age(4)
            const uint8_t* pm = payload.data();
            uint32_t trkId, stat, cls, hits, miss, age;
            double range, az, el, rr, x, y, z, vx, vy, vz, qual;
            pm+=4; std::memcpy(&trkId,pm,4);pm+=4; pm+=8;
            std::memcpy(&stat,pm,4);pm+=4; std::memcpy(&cls,pm,4);pm+=4;
            std::memcpy(&range,pm,8);pm+=8; std::memcpy(&az,pm,8);pm+=8;
            std::memcpy(&el,pm,8);pm+=8;   std::memcpy(&rr,pm,8);pm+=8;
            std::memcpy(&x,pm,8);pm+=8;    std::memcpy(&y,pm,8);pm+=8;
            std::memcpy(&z,pm,8);pm+=8;    std::memcpy(&vx,pm,8);pm+=8;
            std::memcpy(&vy,pm,8);pm+=8;   std::memcpy(&vz,pm,8);pm+=8;
            std::memcpy(&qual,pm,8);pm+=8;
            std::memcpy(&hits,pm,4);pm+=4; std::memcpy(&miss,pm,4);pm+=4;
            std::memcpy(&age,pm,4);

            std::cout << hdr.timestamp << ","
                      << recordTypeName(type) << ","
                      << trkId << ","
                      << std::fixed << std::setprecision(2)
                      << range << ","
                      << az * cuas::RAD2DEG << ","
                      << el * cuas::RAD2DEG << ","
                      << rr << ","
                      << x << "," << y << "," << z << ","
                      << vx << "," << vy << "," << vz << ","
                      << qual << ","
                      << hits << ","
                      << miss << ","
                      << age << ","
                      << stat << ","
                      << cls
                      << std::endl;
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// dat mode: export per-stage .dat files to an output directory
// ---------------------------------------------------------------------------
int datMode(const std::string& filename, const std::string& outDir) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open file: " << filename << std::endl;
        return 1;
    }

    // Create output directory (use filesystem::path for Windows compatibility)
    std::filesystem::path outPath(outDir);
    std::error_code ec;
    std::filesystem::create_directories(outPath, ec);
    if (ec) {
        std::cerr << "ERROR: Cannot create output directory: " << outDir << " - " << ec.message() << std::endl;
        return 1;
    }

    // Open per-stage output files
    auto openDat = [&outPath](const std::string& name) {
        return std::ofstream((outPath / name).string(), std::ios::out | std::ios::trunc);
    };

    std::ofstream fRaw     = openDat("raw_detections.dat");
    std::ofstream fPre     = openDat("preprocessed.dat");
    std::ofstream fCluster = openDat("clusters.dat");
    std::ofstream fPred    = openDat("predictions.dat");
    std::ofstream fAssoc   = openDat("associations.dat");
    std::ofstream fInit    = openDat("tracks_initiated.dat");
    std::ofstream fUpd     = openDat("tracks_updated.dat");
    std::ofstream fDel     = openDat("tracks_deleted.dat");
    std::ofstream fSent    = openDat("tracks_sent.dat");
    std::ofstream fCombined = openDat("combined_track_flow.dat");

    // Write headers
    fRaw     << "timestamp\tdwell_count\tnum_detections\tdet_idx"
             << "\trange\tazimuth_deg\televation_deg\tstrength\tnoise\tsnr\trcs\tmicroDoppler\n";
    fPre     << "timestamp\tnum_detections\tdet_idx"
             << "\trange\tazimuth_deg\televation_deg\tstrength\tnoise\tsnr\trcs\tmicroDoppler\n";
    fCluster << "timestamp\tcluster_id\tnum_detections"
             << "\trange\tazimuth_deg\televation_deg\tstrength\tsnr\trcs\tmicroDoppler\tx\ty\tz\n";
    fPred    << "timestamp\ttrack_id\tx\tvx\tax\ty\tvy\tay\tz\tvz\taz\n";
    fAssoc   << "timestamp\ttrack_id\tcluster_id\tdistance\n";
    fInit    << "timestamp\ttrack_id\tx\tvx\tax\ty\tvy\tay\tz\tvz\taz\n";
    fUpd     << "timestamp\ttrack_id\tstatus\tx\tvx\tax\ty\tvy\tay\tz\tvz\taz\n";
    fDel     << "timestamp\ttrack_id\n";
    fSent    << "timestamp\ttrack_id\tstatus\tclassification\trange\tazimuth_deg\televation_deg"
             << "\trange_rate\tx\ty\tz\tvx\tvy\tvz\tquality\thits\tmisses\tage\n";

    // Combined header written on first data record (or after RunInfo)
    const char* combinedHeader =
        "step\tdwell\ttimestamp_us\tnum_detections\tdet_idx\trange_m\tazimuth_deg\televation_deg\trange_rate"
        "\tstrength\tnoise\tsnr\trcs\tmicroDoppler\tcluster_id\tassoc_distance\ttrack_id\tstatus\tclassification"
        "\tx_m\ty_m\tz_m\tvx\tvy\tvz\tax\tay\taz\tquality\thits\tmisses\tage\n";
    bool combinedHeaderWritten = false;

    cuas::LogRecordHeader hdr;
    uint64_t records = 0;
    uint64_t corruptedDat = 0;
    uint32_t currentDwell = 0;

    while (g_running.load()) {
        if (!cuas::BinaryLogger::readHeader(file, hdr)) {
            if (file.eof()) break;
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            ++corruptedDat;
            continue;
        }
        std::vector<uint8_t> payload;
        if (!cuas::BinaryLogger::readPayload(file, hdr.payloadSize, payload)) {
            if (!cuas::BinaryLogger::resyncToNextRecord(file)) break;
            ++corruptedDat;
            continue;
        }
        ++records;

        auto type = static_cast<cuas::LogRecordType>(hdr.recordType);
        const uint8_t* p = payload.data();

        // Run info: write algo/model details at top of combined file, then column header
        if (type == cuas::LogRecordType::RunInfo) {
            std::string runInfo(payload.begin(), payload.end());
            std::istringstream lines(runInfo);
            std::string line;
            while (std::getline(lines, line))
                fCombined << "# " << line << "\n";
            fCombined << "\n";
            fCombined << combinedHeader;
            combinedHeaderWritten = true;
            continue;
        }

        if (!combinedHeaderWritten) {
            fCombined << combinedHeader;
            combinedHeaderWritten = true;
        }

        switch (type) {
            case cuas::LogRecordType::RawDetection: {
                if (payload.size() < 20) break;
                uint32_t msgId, dwellCount, numDets; uint64_t ts;
                std::memcpy(&msgId,      p, 4); p += 4;
                std::memcpy(&dwellCount, p, 4); p += 4;
                currentDwell = dwellCount;
                std::memcpy(&ts,         p, 8); p += 8;
                std::memcpy(&numDets,    p, 4); p += 4;
                for (uint32_t i = 0; i < numDets && p + sizeof(cuas::Detection) <= payload.data() + payload.size(); ++i) {
                    cuas::Detection d;
                    std::memcpy(&d, p, sizeof(d)); p += sizeof(d);
                    fRaw << std::fixed << std::setprecision(4)
                         << hdr.timestamp << "\t" << dwellCount << "\t" << numDets << "\t" << i
                         << "\t" << d.range << "\t" << d.azimuth * cuas::RAD2DEG
                         << "\t" << d.elevation * cuas::RAD2DEG
                         << "\t" << d.strength << "\t" << d.noise
                         << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler << "\n";
                    fCombined << std::fixed << std::setprecision(4)
                              << "raw\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                              << numDets << "\t" << i << "\t" << d.range << "\t" << d.azimuth * cuas::RAD2DEG
                              << "\t" << d.elevation * cuas::RAD2DEG << "\t\t"
                              << d.strength << "\t" << d.noise << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler
                              << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\n";
                }
                break;
            }
            case cuas::LogRecordType::Preprocessed: {
                if (payload.size() < 4) break;
                uint32_t n; std::memcpy(&n, p, 4); p += 4;
                for (uint32_t i = 0; i < n && p + sizeof(cuas::Detection) <= payload.data() + payload.size(); ++i) {
                    cuas::Detection d;
                    std::memcpy(&d, p, sizeof(d)); p += sizeof(d);
                    fPre << std::fixed << std::setprecision(4)
                         << hdr.timestamp << "\t" << n << "\t" << i
                         << "\t" << d.range << "\t" << d.azimuth * cuas::RAD2DEG
                         << "\t" << d.elevation * cuas::RAD2DEG
                         << "\t" << d.strength << "\t" << d.noise
                         << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler << "\n";
                    fCombined << std::fixed << std::setprecision(4)
                              << "preprocessed\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                              << n << "\t" << i << "\t" << d.range << "\t" << d.azimuth * cuas::RAD2DEG
                              << "\t" << d.elevation * cuas::RAD2DEG << "\t\t"
                              << d.strength << "\t" << d.noise << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler
                              << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\n";
                }
                break;
            }
            case cuas::LogRecordType::Clustered: {
                if (payload.size() < 4) break;
                uint32_t n; std::memcpy(&n, p, 4); p += 4;
                for (uint32_t i = 0; i < n; ++i) {
                    if (p + 4 > payload.data() + payload.size()) break;
                    uint32_t cid; std::memcpy(&cid, p, 4); p += 4;
                    double r, az, el, str, snr, rcs, ud; uint32_t nd;
                    double cx, cy, cz;
                    std::memcpy(&r,   p, 8); p += 8;
                    std::memcpy(&az,  p, 8); p += 8;
                    std::memcpy(&el,  p, 8); p += 8;
                    std::memcpy(&str, p, 8); p += 8;
                    std::memcpy(&snr, p, 8); p += 8;
                    std::memcpy(&rcs, p, 8); p += 8;
                    std::memcpy(&ud,  p, 8); p += 8;
                    std::memcpy(&nd,  p, 4); p += 4;
                    std::memcpy(&cx,  p, 8); p += 8;
                    std::memcpy(&cy,  p, 8); p += 8;
                    std::memcpy(&cz,  p, 8); p += 8;
                    // Skip detectionIndices
                    uint32_t ni; std::memcpy(&ni, p, 4); p += 4;
                    p += ni * sizeof(uint32_t);
                    fCluster << std::fixed << std::setprecision(4)
                             << hdr.timestamp << "\t" << cid << "\t" << nd
                             << "\t" << r << "\t" << az * cuas::RAD2DEG
                             << "\t" << el * cuas::RAD2DEG
                             << "\t" << str << "\t" << snr << "\t" << rcs << "\t" << ud
                             << "\t" << cx << "\t" << cy << "\t" << cz << "\n";
                    fCombined << std::fixed << std::setprecision(4)
                              << "clustering\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                              << nd << "\t\t" << r << "\t" << az * cuas::RAD2DEG << "\t" << el * cuas::RAD2DEG << "\t\t"
                              << str << "\t\t" << snr << "\t" << rcs << "\t" << ud << "\t"
                              << cid << "\t\t\t\t\t\t"
                              << cx << "\t" << cy << "\t" << cz << "\t\t\t\t\t\t\t\t\t\t\t\n";
                }
                break;
            }
            case cuas::LogRecordType::Predicted: {
                if (payload.size() < 4 + cuas::STATE_DIM * 8) break;
                uint32_t tid; std::memcpy(&tid, p, 4); p += 4;
                double sv[cuas::STATE_DIM];
                for (int i = 0; i < cuas::STATE_DIM; ++i) { std::memcpy(&sv[i], p, 8); p += 8; }
                fPred << std::fixed << std::setprecision(4)
                      << hdr.timestamp << "\t" << tid
                      << "\t" << sv[0] << "\t" << sv[1] << "\t" << sv[2]
                      << "\t" << sv[3] << "\t" << sv[4] << "\t" << sv[5]
                      << "\t" << sv[6] << "\t" << sv[7] << "\t" << sv[8] << "\n";
                fCombined << std::fixed << std::setprecision(4)
                          << "prediction\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t\t\t\t\t\t\t\t\t\t\t\t"
                          << tid << "\t\t\t\t"
                          << sv[0] << "\t" << sv[3] << "\t" << sv[6] << "\t"
                          << sv[1] << "\t" << sv[4] << "\t" << sv[7] << "\t"
                          << sv[2] << "\t" << sv[5] << "\t" << sv[8] << "\t\t\t\t\t\n";
                break;
            }
            case cuas::LogRecordType::Associated: {
                if (payload.size() < 16) break;
                uint32_t tid, cid; double dist;
                std::memcpy(&tid,  p, 4); p += 4;
                std::memcpy(&cid,  p, 4); p += 4;
                std::memcpy(&dist, p, 8);
                fAssoc << std::fixed << std::setprecision(4)
                       << hdr.timestamp << "\t" << tid << "\t" << cid << "\t" << dist << "\n";
                fCombined << std::fixed << std::setprecision(4)
                          << "association\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t\t\t\t\t\t\t\t\t\t"
                          << cid << "\t" << dist << "\t" << tid << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\n";
                break;
            }
            case cuas::LogRecordType::TrackInitiated: {
                if (payload.size() < 4 + cuas::STATE_DIM * 8) break;
                uint32_t tid; std::memcpy(&tid, p, 4); p += 4;
                double sv[cuas::STATE_DIM];
                for (int i = 0; i < cuas::STATE_DIM; ++i) { std::memcpy(&sv[i], p, 8); p += 8; }
                fInit << std::fixed << std::setprecision(4)
                      << hdr.timestamp << "\t" << tid
                      << "\t" << sv[0] << "\t" << sv[1] << "\t" << sv[2]
                      << "\t" << sv[3] << "\t" << sv[4] << "\t" << sv[5]
                      << "\t" << sv[6] << "\t" << sv[7] << "\t" << sv[8] << "\n";
                fCombined << std::fixed << std::setprecision(4)
                          << "track_init\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t\t\t\t\t\t\t\t\t\t\t\t"
                          << tid << "\t\t\t\t"
                          << sv[0] << "\t" << sv[3] << "\t" << sv[6] << "\t"
                          << sv[1] << "\t" << sv[4] << "\t" << sv[7] << "\t"
                          << sv[2] << "\t" << sv[5] << "\t" << sv[8] << "\t\t\t\t\t\n";
                break;
            }
            case cuas::LogRecordType::TrackUpdated: {
                if (payload.size() < 8 + cuas::STATE_DIM * 8) break;
                uint32_t tid, status; std::memcpy(&tid, p, 4); p += 4;
                std::memcpy(&status, p, 4); p += 4;
                double sv[cuas::STATE_DIM];
                for (int i = 0; i < cuas::STATE_DIM; ++i) { std::memcpy(&sv[i], p, 8); p += 8; }
                fUpd << std::fixed << std::setprecision(4)
                     << hdr.timestamp << "\t" << tid << "\t" << status
                     << "\t" << sv[0] << "\t" << sv[1] << "\t" << sv[2]
                     << "\t" << sv[3] << "\t" << sv[4] << "\t" << sv[5]
                     << "\t" << sv[6] << "\t" << sv[7] << "\t" << sv[8] << "\n";
                fCombined << std::fixed << std::setprecision(4)
                          << "update\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t\t\t\t\t\t\t\t\t\t\t\t"
                          << tid << "\t" << status << "\t\t"
                          << sv[0] << "\t" << sv[3] << "\t" << sv[6] << "\t"
                          << sv[1] << "\t" << sv[4] << "\t" << sv[7] << "\t"
                          << sv[2] << "\t" << sv[5] << "\t" << sv[8] << "\t\t\t\t\t\n";
                break;
            }
            case cuas::LogRecordType::TrackDeleted: {
                if (payload.size() < 4) break;
                uint32_t tid; std::memcpy(&tid, p, 4);
                fDel << hdr.timestamp << "\t" << tid << "\n";
                fCombined << "track_delete\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t\t\t\t\t\t\t\t\t\t\t\t" << tid << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\n";
                break;
            }
            case cuas::LogRecordType::TrackSent: {
                if (payload.size() < 108u) break;
                // Layout: msgId(4) trkId(4) ts(8) stat(4) cls(4)
                //         range(8) az(8) el(8) rr(8) x(8) y(8) z(8)
                //         vx(8) vy(8) vz(8) qual(8) hits(4) miss(4) age(4)
                uint32_t trkId, stat, cls, hits, miss, age;
                double range, az, el, rr, mx, my, mz, mvx, mvy, mvz, qual;
                p+=4; std::memcpy(&trkId,p,4);p+=4; p+=8;
                std::memcpy(&stat,p,4);p+=4; std::memcpy(&cls,p,4);p+=4;
                std::memcpy(&range,p,8);p+=8; std::memcpy(&az,p,8);p+=8;
                std::memcpy(&el,p,8);p+=8;   std::memcpy(&rr,p,8);p+=8;
                std::memcpy(&mx,p,8);p+=8;   std::memcpy(&my,p,8);p+=8;
                std::memcpy(&mz,p,8);p+=8;   std::memcpy(&mvx,p,8);p+=8;
                std::memcpy(&mvy,p,8);p+=8;  std::memcpy(&mvz,p,8);p+=8;
                std::memcpy(&qual,p,8);p+=8;
                std::memcpy(&hits,p,4);p+=4; std::memcpy(&miss,p,4);p+=4;
                std::memcpy(&age,p,4);
                fSent << std::fixed << std::setprecision(4)
                      << hdr.timestamp << "\t" << trkId
                      << "\t" << stat << "\t" << cls
                      << "\t" << range
                      << "\t" << az * cuas::RAD2DEG
                      << "\t" << el * cuas::RAD2DEG
                      << "\t" << rr
                      << "\t" << mx << "\t" << my << "\t" << mz
                      << "\t" << mvx << "\t" << mvy << "\t" << mvz
                      << "\t" << qual
                      << "\t" << hits << "\t" << miss << "\t" << age << "\n";
                fCombined << std::fixed << std::setprecision(4)
                          << "sender\t" << currentDwell << "\t" << hdr.timestamp << "\t"
                          << "\t\t" << range << "\t" << az * cuas::RAD2DEG
                          << "\t" << el * cuas::RAD2DEG << "\t" << rr << "\t\t\t\t\t\t\t\t\t\t"
                          << trkId << "\t" << stat << "\t" << cls << "\t"
                          << mx << "\t" << my << "\t" << mz << "\t"
                          << mvx << "\t" << mvy << "\t" << mvz << "\t\t\t\t\t"
                          << qual << "\t" << hits << "\t" << miss << "\t" << age << "\n";
                break;
            }
            default: break;
        }
    }

    std::cout << "Exported " << records << " records to: " << outDir << std::endl;
    std::cout << "  raw_detections.dat  preprocessed.dat  clusters.dat" << std::endl;
    std::cout << "  predictions.dat     associations.dat" << std::endl;
    std::cout << "  tracks_initiated.dat  tracks_updated.dat  tracks_deleted.dat  tracks_sent.dat" << std::endl;
    std::cout << "  combined_track_flow.dat (all steps in dwell-wise order)" << std::endl;
    if (corruptedDat > 0)
        std::cerr << "WARNING: " << corruptedDat << " corrupted/truncated record(s) skipped.\n";
    return 0;
}

int main(int argc, char* argv[]) {
    std::cout.flush();
    std::cerr.flush();

    if (argc < 2) {
        std::cerr << "Counter-UAS Radar Tracker - Log Extractor & Replay Tool" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Usage: " << argv[0] << " <logfile> [mode] [options]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Modes:" << std::endl;
        std::cerr << "  extract [verbose]              - Extract and print log contents" << std::endl;
        std::cerr << "  replay [ip] [port] [speed]     - Replay detections via UDP" << std::endl;
        std::cerr << "  csv                            - Export track data as CSV" << std::endl;
        std::cerr << "  dat [output_dir]               - Export per-stage .dat files" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_log.bin extract" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_log.bin extract verbose" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_log.bin replay 127.0.0.1 50000 2.0" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_log.bin csv > tracks.csv" << std::endl;
        std::cerr << "  " << argv[0] << " tracker_log.bin dat ./exported_data" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Note: Provide a log file path (e.g. ./logs/tracker_log.bin) from a previous tracker run." << std::endl;
        return 1;
    }

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

    std::string filename = argv[1];
    std::string mode     = (argc > 2) ? argv[2] : "extract";

    // Check log file exists before proceeding
    if (!std::ifstream(filename, std::ios::binary).good()) {
        std::cerr << "ERROR: Cannot open log file: " << filename << std::endl;
        std::cerr << "       Check the path and that the tracker has created a log (logEnabled in config)." << std::endl;
        return 1;
    }

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
    } else if (mode == "dat") {
        std::string outDir = (argc > 3) ? argv[3] : "./dat_export";
        return datMode(filename, outDir);
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        return 1;
    }
}
