#pragma once

#include "types.h"
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <vector>

namespace cuas {

class BinaryLogger {
public:
    BinaryLogger();
    ~BinaryLogger();

    bool open(const std::string& directory, const std::string& prefix);
    void close();
    bool isOpen() const;

    void logRawDetections(Timestamp ts, const SPDetectionMessage& msg);
    void logPreprocessed(Timestamp ts, const std::vector<Detection>& dets);
    void logClustered(Timestamp ts, const std::vector<Cluster>& clusters);
    void logPredicted(Timestamp ts, uint32_t trackId, const StateVector& state);
    void logAssociated(Timestamp ts, uint32_t trackId, uint32_t clusterId, double distance);
    void logTrackInitiated(Timestamp ts, uint32_t trackId, const StateVector& state);
    void logTrackUpdated(Timestamp ts, uint32_t trackId, const StateVector& state, TrackStatus status);
    void logTrackDeleted(Timestamp ts, uint32_t trackId);
    void logTrackSent(Timestamp ts, const TrackUpdateMessage& msg);

    // Read interface for extractor
    static bool readHeader(std::ifstream& in, LogRecordHeader& hdr);
    static bool readPayload(std::ifstream& in, uint32_t size, std::vector<uint8_t>& data);

private:
    void writeRecord(LogRecordType type, Timestamp ts, const void* data, uint32_t size);
    void writeRecord(LogRecordType type, Timestamp ts, const std::vector<uint8_t>& data);

    std::ofstream file_;
    std::mutex    mutex_;
    bool          open_ = false;
};

class ConsoleLogger {
public:
    enum Level { ERROR = 0, WARN = 1, INFO = 2, DEBUG = 3, TRACE = 4 };

    static ConsoleLogger& instance();

    void setLevel(Level lvl);
    Level level() const { return level_; }

    void error(const char* module, const char* fmt, ...);
    void warn(const char* module, const char* fmt, ...);
    void info(const char* module, const char* fmt, ...);
    void debug(const char* module, const char* fmt, ...);
    void trace(const char* module, const char* fmt, ...);

private:
    ConsoleLogger() = default;
    void log(Level lvl, const char* module, const char* fmt, va_list args);

    Level level_ = Level::INFO;
    std::mutex mutex_;
};

#define LOG_ERROR(mod, ...) cuas::ConsoleLogger::instance().error(mod, __VA_ARGS__)
#define LOG_WARN(mod, ...)  cuas::ConsoleLogger::instance().warn(mod, __VA_ARGS__)
#define LOG_INFO(mod, ...)  cuas::ConsoleLogger::instance().info(mod, __VA_ARGS__)
#define LOG_DEBUG(mod, ...) cuas::ConsoleLogger::instance().debug(mod, __VA_ARGS__)
#define LOG_TRACE(mod, ...) cuas::ConsoleLogger::instance().trace(mod, __VA_ARGS__)

} // namespace cuas
