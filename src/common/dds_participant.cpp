#include "common/dds_participant.h"
#include "common/logger.h"
#include <stdexcept>

namespace cuas {

CuasDdsParticipant::CuasDdsParticipant(uint8_t domainId) {
    auto factory = eprosima::fastdds::dds::DomainParticipantFactory::get_instance();

    participant_ = factory->create_participant(
        domainId,
        eprosima::fastdds::dds::PARTICIPANT_QOS_DEFAULT);
    if (!participant_)
        throw std::runtime_error("DDS: failed to create DomainParticipant");

    publisher_ = participant_->create_publisher(
        eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT);
    if (!publisher_)
        throw std::runtime_error("DDS: failed to create Publisher");

    subscriber_ = participant_->create_subscriber(
        eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT);
    if (!subscriber_)
        throw std::runtime_error("DDS: failed to create Subscriber");

    LOG_INFO("DDS", "DomainParticipant created on domain %u", domainId);
}

CuasDdsParticipant::~CuasDdsParticipant() {
    if (participant_) {
        participant_->delete_contained_entities();
        eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
            ->delete_participant(participant_);
        LOG_INFO("DDS", "DomainParticipant destroyed");
    }
}

} // namespace cuas
