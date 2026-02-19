#include "common/logger.h"
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

bool BinaryLogger::open(const std::string& directory, const std::string& prefix) {
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

    open_ = true;
    LOG_INFO("BinaryLogger", "Opened log file: %s", fname.str().c_str());
    return true;
}

void BinaryLogger::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (open_) {
        file_.flush();
        file_.close();
        open_ = false;
    }
}

bool BinaryLogger::isOpen() const { return open_; }

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
}

void BinaryLogger::writeRecord(LogRecordType type, Timestamp ts,
                                const std::vector<uint8_t>& data) {
    writeRecord(type, ts, data.data(), static_cast<uint32_t>(data.size()));
}

void BinaryLogger::logRawDetections(Timestamp ts, const SPDetectionMessage& msg) {
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
}

void BinaryLogger::logPredicted(Timestamp ts, uint32_t trackId, const StateVector& state) {
    std::vector<uint8_t> buf(sizeof(uint32_t) + STATE_DIM * sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    for (int i = 0; i < STATE_DIM; ++i) {
        std::memcpy(p, &state[i], 8); p += 8;
    }
    writeRecord(LogRecordType::Predicted, ts, buf);
}

void BinaryLogger::logAssociated(Timestamp ts, uint32_t trackId,
                                  uint32_t clusterId, double distance) {
    std::vector<uint8_t> buf(sizeof(uint32_t) * 2 + sizeof(double));
    uint8_t* p = buf.data();
    std::memcpy(p, &trackId, 4); p += 4;
    std::memcpy(p, &clusterId, 4); p += 4;
    std::memcpy(p, &distance, 8); p += 8;
    writeRecord(LogRecordType::Associated, ts, buf);
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
}

void BinaryLogger::logTrackDeleted(Timestamp ts, uint32_t trackId) {
    writeRecord(LogRecordType::TrackDeleted, ts, &trackId, sizeof(uint32_t));
}

void BinaryLogger::logTrackSent(Timestamp ts, const TrackUpdateMessage& msg) {
    writeRecord(LogRecordType::TrackSent, ts, &msg, sizeof(TrackUpdateMessage));
}

bool BinaryLogger::readHeader(std::ifstream& in, LogRecordHeader& hdr) {
    in.read(reinterpret_cast<char*>(&hdr), sizeof(LogRecordHeader));
    return in.good() && hdr.magic == LOG_MAGIC;
}

bool BinaryLogger::readPayload(std::ifstream& in, uint32_t size,
                                std::vector<uint8_t>& data) {
    data.resize(size);
    in.read(reinterpret_cast<char*>(data.data()), size);
    return in.good();
}

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
