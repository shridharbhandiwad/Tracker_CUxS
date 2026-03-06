#include "common/logger.h"
#include "common/constants.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

#ifdef _WIN32
    #include <direct.h>
    #define MKDIR(d) _mkdir(d)
#else
    #include <sys/stat.h>
    #define MKDIR(d) mkdir(d, 0755)
#endif

namespace cuas {

// ---------------------------------------------------------------------------
// BinaryLogger
// ---------------------------------------------------------------------------

BinaryLogger::BinaryLogger() = default;

BinaryLogger::~BinaryLogger() {
    close();
}

bool BinaryLogger::open(const std::string& directory, const std::string& prefix,
                        const std::string& runInfo) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) return true;

    MKDIR(directory.c_str());

    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    std::ostringstream fname;
    fname << directory << "/" << prefix << "_"
          << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".bin";

    file_.open(fname.str(), std::ios::binary | std::ios::out);
    if (!file_.is_open()) return false;

    logPath_ = fname.str();

    // If run info provided, write as first record to .bin (so extractor can show it).
    // Write directly to file_ without calling writeRecord() to avoid deadlock (we already hold mutex_).
    if (!runInfo.empty()) {
        LogRecordHeader hdr;
        hdr.magic       = LOG_MAGIC;
        hdr.recordType  = static_cast<uint32_t>(LogRecordType::RunInfo);
        hdr.timestamp   = 0;
        hdr.payloadSize = static_cast<uint32_t>(runInfo.size());
        file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        file_.write(runInfo.data(), static_cast<std::streamsize>(runInfo.size()));
        // EOM sentinel after RunInfo payload
        const uint32_t eom = LOG_EOM;
        file_.write(reinterpret_cast<const char*>(&eom), sizeof(eom));
    }

    std::string combinedPath = fname.str();
    size_t dot = combinedPath.rfind(".bin");
    if (dot != std::string::npos)
        combinedPath = combinedPath.substr(0, dot) + "_combined_track_flow.dat";
    else
        combinedPath += "_combined_track_flow.dat";
    combinedDat_.open(combinedPath, std::ios::out);
    if (combinedDat_.is_open()) {
        // Run details at top (algorithms/models used in this run)
        if (!runInfo.empty()) {
            std::istringstream lines(runInfo);
            std::string line;
            while (std::getline(lines, line))
                combinedDat_ << "# " << line << "\n";
            combinedDat_ << "\n";
        }
        combinedDat_ << "step\tdwell\ttimestamp_us\tnum_detections\tdet_idx\trange_m\tazimuth_deg\televation_deg\trange_rate"
                    << "\tstrength\tnoise\tsnr\trcs\tmicroDoppler\tcluster_id\tassoc_distance\ttrack_id\tstatus\tclassification"
                    << "\tx_m\ty_m\tz_m\tvx\tvy\tvz\tax\tay\taz\tquality\thits\tmisses\tage\n";
    }

    open_ = true;
    LOG_INFO("BinaryLogger", "Opened log file: %s", fname.str().c_str());
    return true;
}

void BinaryLogger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) {
        file_.flush();
        file_.close();
        if (combinedDat_.is_open()) {
            combinedDat_.flush();
            combinedDat_.close();
        }
        open_ = false;
    }
}

bool BinaryLogger::isOpen() const { return open_; }

void BinaryLogger::writeCombinedLine(const char* step, Timestamp ts, const std::string& payload) {
    if (!combinedDat_.is_open()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    combinedDat_ << step << "\t" << currentDwell_ << "\t" << ts << "\t" << payload << "\n";
}

void BinaryLogger::writeRecord(LogRecordType type, Timestamp ts,
                                const void* data, uint32_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return;

    LogRecordHeader hdr;
    hdr.magic       = LOG_MAGIC;
    hdr.recordType  = static_cast<uint32_t>(type);
    hdr.timestamp   = ts;
    hdr.payloadSize = size;

    file_.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (size > 0 && data) {
        file_.write(reinterpret_cast<const char*>(data), size);
    }
    // EOM sentinel — always written, even for zero-byte payloads
    const uint32_t eom = LOG_EOM;
    file_.write(reinterpret_cast<const char*>(&eom), sizeof(eom));
}

void BinaryLogger::writeRecord(LogRecordType type, Timestamp ts,
                                const std::vector<uint8_t>& data) {
    writeRecord(type, ts, data.data(), static_cast<uint32_t>(data.size()));
}

void BinaryLogger::logRawDetections(Timestamp ts, const SPDetectionMessage& msg) {
    currentDwell_ = msg.dwellCount;
    std::vector<uint8_t> buf;
    uint32_t n = msg.numDetections;
    size_t sz = sizeof(uint32_t) * 3 + sizeof(Timestamp) + n * sizeof(Detection);
    buf.resize(sz);

    uint8_t* p = buf.data();
    std::memcpy(p, &msg.messageId, 4); p += 4;
    std::memcpy(p, &msg.dwellCount, 4); p += 4;
    std::memcpy(p, &msg.timestamp, 8); p += 8;
    std::memcpy(p, &n, 4); p += 4;
    for (uint32_t i = 0; i < n; ++i) {
        std::memcpy(p, &msg.detections[i], sizeof(Detection));
        p += sizeof(Detection);
    }
    writeRecord(LogRecordType::RawDetection, ts, buf);
    for (uint32_t i = 0; i < n; ++i) {
        const Detection& d = msg.detections[i];
        std::ostringstream pl;
        pl << std::fixed << std::setprecision(4) << n << "\t" << i << "\t" << d.range
           << "\t" << (d.azimuth * RAD2DEG) << "\t" << (d.elevation * RAD2DEG) << "\t\t"
           << d.strength << "\t" << d.noise << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler
           << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
        writeCombinedLine("raw", ts, pl.str());
    }
}

void BinaryLogger::logPreprocessed(Timestamp ts, const std::vector<Detection>& dets) {
    uint32_t n = static_cast<uint32_t>(dets.size());
    std::vector<uint8_t> buf(sizeof(uint32_t) + n * sizeof(Detection));
    uint8_t* p = buf.data();
    std::memcpy(p, &n, 4); p += 4;
    for (auto& d : dets) {
        std::memcpy(p, &d, sizeof(Detection));
        p += sizeof(Detection);
    }
    writeRecord(LogRecordType::Preprocessed, ts, buf);
    for (uint32_t i = 0; i < n; ++i) {
        const Detection& d = dets[i];
        std::ostringstream pl;
        pl << std::fixed << std::setprecision(4) << n << "\t" << i << "\t" << d.range
           << "\t" << (d.azimuth * RAD2DEG) << "\t" << (d.elevation * RAD2DEG) << "\t\t"
           << d.strength << "\t" << d.noise << "\t" << d.snr << "\t" << d.rcs << "\t" << d.microDoppler
           << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
        writeCombinedLine("preprocessed", ts, pl.str());
    }
}

void BinaryLogger::logClustered(Timestamp ts, const std::vector<Cluster>& clusters) {
    uint32_t n = static_cast<uint32_t>(clusters.size());
    size_t sz = sizeof(uint32_t);
    for (auto& c : clusters) {
        sz += sizeof(uint32_t) + 7 * sizeof(double) + sizeof(uint32_t) +
              3 * sizeof(double) + sizeof(uint32_t) +
              c.detectionIndices.size() * sizeof(uint32_t);
    }
    std::vector<uint8_t> buf(sz);
    uint8_t* p = buf.data();
    std::memcpy(p, &n, 4); p += 4;
    for (auto& c : clusters) {
        std::memcpy(p, &c.clusterId, 4); p += 4;
        std::memcpy(p, &c.range, 8); p += 8;
        std::memcpy(p, &c.azimuth, 8); p += 8;
        std::memcpy(p, &c.elevation, 8); p += 8;
        std::memcpy(p, &c.strength, 8); p += 8;
        std::memcpy(p, &c.snr, 8); p += 8;
        std::memcpy(p, &c.rcs, 8); p += 8;
        std::memcpy(p, &c.microDoppler, 8); p += 8;
        std::memcpy(p, &c.numDetections, 4); p += 4;
        std::memcpy(p, &c.cartesian.x, 8); p += 8;
        std::memcpy(p, &c.cartesian.y, 8); p += 8;
        std::memcpy(p, &c.cartesian.z, 8); p += 8;
        uint32_t ni = static_cast<uint32_t>(c.detectionIndices.size());
        std::memcpy(p, &ni, 4); p += 4;
        for (auto idx : c.detectionIndices) {
            std::memcpy(p, &idx, 4); p += 4;
        }
    }
    writeRecord(LogRecordType::Clustered, ts, buf);
    for (const auto& c : clusters) {
        std::ostringstream pl;
        pl << std::fixed << std::setprecision(4) << c.numDetections << "\t\t" << c.range
           << "\t" << (c.azimuth * RAD2DEG) << "\t" << (c.elevation * RAD2DEG) << "\t\t"
           << c.strength << "\t\t" << c.snr << "\t" << c.rcs << "\t" << c.microDoppler
           << "\t" << c.clusterId << "\t\t\t\t\t\t"
           << c.cartesian.x << "\t" << c.cartesian.y << "\t" << c.cartesian.z
           << "\t\t\t\t\t\t\t\t\t\t";
        writeCombinedLine("clustering", ts, pl.str());
    }
}

void BinaryLogger::logPredicted(Timestamp ts, uint32_t trackId, const StateVector& state) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + STATE_DIM * sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    for (int i = 0; i < STATE_DIM; ++i) {
        std::memcpy(p, &state[i], 8); p += 8;
    }
    writeRecord(LogRecordType::Predicted, ts, buf);
    std::ostringstream pl;
    pl << std::fixed << std::setprecision(4)
       << "\t\t\t\t\t\t\t\t\t\t\t\t\t" << trackId << "\t\t\t\t"
       << state[0] << "\t" << state[3] << "\t" << state[6] << "\t"
       << state[1] << "\t" << state[4] << "\t" << state[7] << "\t"
       << state[2] << "\t" << state[5] << "\t" << state[8] << "\t\t\t\t\t";
    writeCombinedLine("prediction", ts, pl.str());
}

void BinaryLogger::logAssociated(Timestamp ts, uint32_t trackId,
                                  uint32_t clusterId, double distance) {
    std::vector<uint8_t> buf(sizeof(uint32_t) * 2 + sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    std::memcpy(p, &clusterId, 4); p += 4;
    std::memcpy(p, &distance, 8); p += 8;
    writeRecord(LogRecordType::Associated, ts, buf);
    std::ostringstream pl;
    pl << std::fixed << std::setprecision(4)
       << "\t\t\t\t\t\t\t\t\t\t\t" << clusterId << "\t" << distance << "\t" << trackId << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
    writeCombinedLine("association", ts, pl.str());
}

void BinaryLogger::logTrackInitiated(Timestamp ts, uint32_t trackId,
                                      const StateVector& state) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + STATE_DIM * sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    for (int i = 0; i < STATE_DIM; ++i) {
        std::memcpy(p, &state[i], 8); p += 8;
    }
    writeRecord(LogRecordType::TrackInitiated, ts, buf);
    std::ostringstream pl;
    pl << std::fixed << std::setprecision(4)
       << "\t\t\t\t\t\t\t\t\t\t\t\t\t" << trackId << "\t\t\t\t"
       << state[0] << "\t" << state[3] << "\t" << state[6] << "\t"
       << state[1] << "\t" << state[4] << "\t" << state[7] << "\t"
       << state[2] << "\t" << state[5] << "\t" << state[8] << "\t\t\t\t\t";
    writeCombinedLine("track_init", ts, pl.str());
}

void BinaryLogger::logTrackUpdated(Timestamp ts, uint32_t trackId,
                                    const StateVector& state, TrackStatus status) {
    std::vector<uint8_t> buf(sizeof(uint32_t) * 2 + STATE_DIM * sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    uint32_t s = static_cast<uint32_t>(status);
    std::memcpy(p, &s, 4); p += 4;
    for (int i = 0; i < STATE_DIM; ++i) {
        std::memcpy(p, &state[i], 8); p += 8;
    }
    writeRecord(LogRecordType::TrackUpdated, ts, buf);
    std::ostringstream pl;
    pl << std::fixed << std::setprecision(4)
       << "\t\t\t\t\t\t\t\t\t\t\t\t\t" << trackId << "\t" << s << "\t\t"
       << state[0] << "\t" << state[3] << "\t" << state[6] << "\t"
       << state[1] << "\t" << state[4] << "\t" << state[7] << "\t"
       << state[2] << "\t" << state[5] << "\t" << state[8] << "\t\t\t\t\t";
    writeCombinedLine("update", ts, pl.str());
}

void BinaryLogger::logTrackDeleted(Timestamp ts, uint32_t trackId) {
    writeRecord(LogRecordType::TrackDeleted, ts, &trackId, sizeof(uint32_t));
    std::ostringstream pl;
    pl << "\t\t\t\t\t\t\t\t\t\t\t\t\t" << trackId << "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
    writeCombinedLine("track_delete", ts, pl.str());
}

void BinaryLogger::logTrackSent(Timestamp ts, const TrackUpdateMessage& msg) {
    writeRecord(LogRecordType::TrackSent, ts, &msg, sizeof(TrackUpdateMessage));
    std::ostringstream pl;
    pl << std::fixed << std::setprecision(4)
       << "\t\t" << msg.range << "\t" << (msg.azimuth * RAD2DEG) << "\t" << (msg.elevation * RAD2DEG)
       << "\t" << msg.rangeRate << "\t\t\t\t\t\t\t\t\t\t"
       << msg.trackId << "\t" << static_cast<int>(msg.status) << "\t" << static_cast<int>(msg.classification) << "\t"
       << msg.x << "\t" << msg.y << "\t" << msg.z
       << "\t" << msg.vx << "\t" << msg.vy << "\t" << msg.vz << "\t\t\t\t\t"
       << msg.trackQuality << "\t" << msg.hitCount << "\t" << msg.missCount << "\t" << msg.age;
    writeCombinedLine("sender", ts, pl.str());
}

bool BinaryLogger::readHeader(std::ifstream& in, LogRecordHeader& hdr) {
    in.read(reinterpret_cast<char*>(&hdr), sizeof(LogRecordHeader));
    return in.good() && hdr.magic == LOG_MAGIC;
}

bool BinaryLogger::readPayload(std::ifstream& in, uint32_t size,
                                std::vector<uint8_t>& data) {
    // Guard against a corrupted payloadSize that would cause a huge allocation.
    if (size > LOG_MAX_PAYLOAD) return false;

    data.resize(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), size);
        if (!in.good()) return false;
    }

    // Verify the EOM sentinel that follows every record payload.
    uint32_t eom = 0;
    in.read(reinterpret_cast<char*>(&eom), sizeof(eom));
    return in.good() && (eom == LOG_EOM);
}

bool BinaryLogger::resyncToNextRecord(std::ifstream& in) {
    // LOG_MAGIC (0xCAFEBABE) as it appears in the file (little-endian byte order).
    const uint8_t magic_le[4] = {
        static_cast<uint8_t>(LOG_MAGIC & 0xFFu),
        static_cast<uint8_t>((LOG_MAGIC >> 8)  & 0xFFu),
        static_cast<uint8_t>((LOG_MAGIC >> 16) & 0xFFu),
        static_cast<uint8_t>((LOG_MAGIC >> 24) & 0xFFu)
    };

    // Slide a 4-byte window through the stream one byte at a time.
    // None of the four bytes of LOG_MAGIC repeat within the pattern, so a simple
    // greedy match is sufficient (no KMP partial-match table needed).
    int matched = 0;
    char byte;
    while (in.read(&byte, 1)) {
        const uint8_t b = static_cast<uint8_t>(byte);
        if (b == magic_le[matched]) {
            ++matched;
            if (matched == 4) {
                // Rewind so readHeader() reads from the SOM sentinel.
                in.seekg(-4, std::ios::cur);
                return in.good();
            }
        } else {
            matched = (b == magic_le[0]) ? 1 : 0;
        }
    }
    return false;
}

std::string BinaryLogger::getLogPath() const { return logPath_; }

// ---------------------------------------------------------------------------
// ConsoleLogger
// ---------------------------------------------------------------------------

ConsoleLogger& ConsoleLogger::instance() {
    static ConsoleLogger inst;
    return inst;
}

void ConsoleLogger::setLevel(Level lvl) { level_ = lvl; }

void ConsoleLogger::log(Level lvl, const char* module, const char* fmt, va_list args) {
    if (lvl > level_) return;
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    static const char* levelNames[] = {"ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"};

    char timeBuf[32];
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d",
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));

    char msgBuf[2048];
    std::vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);

    std::fprintf(stderr, "[%s] [%s] [%-16s] %s\n",
                 timeBuf, levelNames[static_cast<int>(lvl)], module, msgBuf);
}

void ConsoleLogger::error(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt); log(ERROR, module, fmt, args); va_end(args);
}

void ConsoleLogger::warn(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt); log(WARN, module, fmt, args); va_end(args);
}

void ConsoleLogger::info(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt); log(INFO, module, fmt, args); va_end(args);
}

void ConsoleLogger::debug(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt); log(DEBUG, module, fmt, args); va_end(args);
}

void ConsoleLogger::trace(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt); log(TRACE, module, fmt, args); va_end(args);
}

} // namespace cuas
