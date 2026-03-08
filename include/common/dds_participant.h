#pragma once

/*
 * DDS Participant factory and helper templates.
 *
 * Every process that uses IDL-generated types communicates via a single
 * DomainParticipant on domain 0.  This header provides:
 *
 *   CuasDdsParticipant  – RAII wrapper around a DomainParticipant plus its
 *                          Publisher and Subscriber.  Owns all DDS entities.
 *
 *   makeWriter<T>()     – create a strongly-typed DataWriter for topic T.
 *   makeReader<T>()     – create a strongly-typed DataReader for topic T
 *                          with an optional DataReaderListener.
 *
 * Usage (producer side):
 *
 *   CuasDdsParticipant part;
 *   auto* writer = part.makeWriter<CounterUAS::SPDetectionMessage>(
 *                      TOPIC_SP_DETECTION);
 *   CounterUAS::SPDetectionMessage msg;
 *   // ... populate via setters ...
 *   writer->write(&msg);
 *
 * Usage (consumer side):
 *
 *   class MyListener : public eprosima::fastdds::dds::DataReaderListener { ... };
 *   CuasDdsParticipant part;
 *   MyListener listener;
 *   part.makeReader<CounterUAS::TrackTableMessage>(TOPIC_TRACK_TABLE, &listener);
 */

#include "messages.h"
#include "messagesPubSubTypes.h"

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <string>
#include <stdexcept>

namespace cuas {

// ---------------------------------------------------------------------------
// Trait: maps a generated IDL struct type to its PubSubType class.
// Specialisations for all five topics are defined below.
// ---------------------------------------------------------------------------
template <typename T>
struct DdsPubSubType;

template<> struct DdsPubSubType<CounterUAS::SPDetectionMessage> {
    using type = CounterUAS::SPDetectionMessagePubSubType; };
template<> struct DdsPubSubType<CounterUAS::TrackTableMessage> {
    using type = CounterUAS::TrackTableMessagePubSubType; };
template<> struct DdsPubSubType<CounterUAS::ClusterTableMessage> {
    using type = CounterUAS::ClusterTableMessagePubSubType; };
template<> struct DdsPubSubType<CounterUAS::AssocTableMessage> {
    using type = CounterUAS::AssocTableMessagePubSubType; };
template<> struct DdsPubSubType<CounterUAS::PredictedTableMessage> {
    using type = CounterUAS::PredictedTableMessagePubSubType; };

// ---------------------------------------------------------------------------
// CuasDdsParticipant
// ---------------------------------------------------------------------------
class CuasDdsParticipant {
public:
    explicit CuasDdsParticipant(uint8_t domainId = 0);
    ~CuasDdsParticipant();

    CuasDdsParticipant(const CuasDdsParticipant&)            = delete;
    CuasDdsParticipant& operator=(const CuasDdsParticipant&) = delete;

    // Register type + create topic + create DataWriter.
    template <typename T>
    eprosima::fastdds::dds::DataWriter* makeWriter(const std::string& topicName) {
        using PST = typename DdsPubSubType<T>::type;
        eprosima::fastdds::dds::TypeSupport ts(new PST());
        ts.register_type(participant_);

        auto* topic = participant_->create_topic(
            topicName, ts->getName(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        if (!topic)
            throw std::runtime_error("DDS: create_topic failed for " + topicName);

        auto* writer = publisher_->create_datawriter(
            topic, eprosima::fastdds::dds::DATAWRITER_QOS_DEFAULT);
        if (!writer)
            throw std::runtime_error("DDS: create_datawriter failed for " + topicName);
        return writer;
    }

    // Register type + create topic + create DataReader (optional listener).
    template <typename T>
    eprosima::fastdds::dds::DataReader* makeReader(
            const std::string& topicName,
            eprosima::fastdds::dds::DataReaderListener* listener = nullptr) {
        using PST = typename DdsPubSubType<T>::type;
        eprosima::fastdds::dds::TypeSupport ts(new PST());
        ts.register_type(participant_);

        auto* topic = participant_->create_topic(
            topicName, ts->getName(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        if (!topic)
            throw std::runtime_error("DDS: create_topic failed for " + topicName);

        eprosima::fastdds::dds::DataReaderQos qos =
            eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
        auto* reader = subscriber_->create_datareader(topic, qos, listener);
        if (!reader)
            throw std::runtime_error("DDS: create_datareader failed for " + topicName);
        return reader;
    }

private:
    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Publisher*         publisher_   = nullptr;
    eprosima::fastdds::dds::Subscriber*        subscriber_  = nullptr;
};

} // namespace cuas
