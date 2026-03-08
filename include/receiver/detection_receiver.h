#pragma once

/*
 * DetectionReceiver — DDS subscriber for the SPDetection topic.
 *
 * The DSP injector publishes CounterUAS::SPDetectionMessage on the DDS topic
 * "SPDetection".  This class subscribes to that topic and converts each
 * received message to the internal cuas::SPDetectionMessage type before
 * invoking the user callback.
 *
 * The DDS DataReaderListener runs on the DDS middleware thread.  The callback
 * is therefore invoked from that thread; callers must be thread-safe.
 */

#include "common/types.h"
#include "common/dds_participant.h"
#include "common/config.h"

#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>

#include <functional>
#include <atomic>

namespace cuas {

class DetectionReceiver
    : public eprosima::fastdds::dds::DataReaderListener {
public:
    using Callback = std::function<void(const SPDetectionMessage&)>;

    explicit DetectionReceiver(CuasDdsParticipant& participant,
                               const std::string& topicName);
    ~DetectionReceiver() override = default;

    DetectionReceiver(const DetectionReceiver&)            = delete;
    DetectionReceiver& operator=(const DetectionReceiver&) = delete;

    void setCallback(Callback cb) { callback_ = std::move(cb); }

    // DataReaderListener overrides
    void on_data_available(
        eprosima::fastdds::dds::DataReader* reader) override;

    uint64_t totalMessagesReceived()   const { return msgCount_.load(); }
    uint64_t totalDetectionsReceived() const { return detCount_.load(); }

private:
    Callback  callback_;
    std::atomic<uint64_t> msgCount_{0};
    std::atomic<uint64_t> detCount_{0};
};

} // namespace cuas
