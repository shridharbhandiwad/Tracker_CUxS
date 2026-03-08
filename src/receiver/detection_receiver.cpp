#include "receiver/detection_receiver.h"
#include "common/constants.h"
#include "common/logger.h"

#include <fastdds/dds/subscriber/SampleInfo.hpp>

namespace cuas {

DetectionReceiver::DetectionReceiver(CuasDdsParticipant& participant,
                                     const std::string& topicName) {
    // Register the SPDetectionMessage type and create the DataReader.
    // The listener (this) will be invoked on the DDS thread whenever
    // new data arrives on the topic.
    participant.makeReader<CounterUAS::SPDetectionMessage>(topicName, this);
    LOG_INFO("Receiver", "DDS subscriber created on topic '%s'", topicName.c_str());
}

void DetectionReceiver::on_data_available(
    eprosima::fastdds::dds::DataReader* reader) {

    CounterUAS::SPDetectionMessage idlMsg;
    eprosima::fastdds::dds::SampleInfo info;

    while (eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK ==
           reader->take_next_sample(&idlMsg, &info)) {

        if (!info.valid_data) continue;

        // Convert from IDL wire type to internal pipeline type.
        SPDetectionMessage msg = toInternal(idlMsg);

        msgCount_.fetch_add(1);
        detCount_.fetch_add(msg.numDetections);

        LOG_DEBUG("Receiver", "Dwell %u: %u detections (DDS topic)",
                  msg.dwellCount, msg.numDetections);

        if (callback_) {
            callback_(msg);
        }
    }
}

} // namespace cuas
