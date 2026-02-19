#include "pipeline/tracker_pipeline.h"
#include "common/logger.h"
#include <chrono>

namespace cuas {

TrackerPipeline::TrackerPipeline(const TrackerConfig& cfg) : config_(cfg) {}

TrackerPipeline::~TrackerPipeline() {
    stop();
}

bool TrackerPipeline::start() {
    LOG_INFO("Pipeline", "Starting tracker pipeline...");

    receiver_     = std::make_unique<DetectionReceiver>(config_.network);
    trackManager_ = std::make_unique<TrackManager>(config_);
    sender_       = std::make_unique<TrackSender>(config_.network, config_.display);

    if (!sender_->init()) {
        LOG_ERROR("Pipeline", "Failed to initialize track sender");
        return false;
    }

    running_.store(true);
    processingThread_ = std::thread(&TrackerPipeline::processingLoop, this);

    if (!receiver_->start([this](const SPDetectionMessage& msg) {
            onDetectionReceived(msg);
        })) {
        LOG_ERROR("Pipeline", "Failed to start detection receiver");
        stop();
        return false;
    }

    LOG_INFO("Pipeline", "Tracker pipeline started successfully");
    return true;
}

void TrackerPipeline::stop() {
    running_.store(false);
    queueCV_.notify_all();

    if (receiver_) receiver_->stop();
    if (processingThread_.joinable()) processingThread_.join();
    if (sender_) sender_->close();

    LOG_INFO("Pipeline", "Tracker pipeline stopped. Total cycles: %lu", 
             static_cast<unsigned long>(cycleCount_));
    printStats();
}

void TrackerPipeline::onDetectionReceived(const SPDetectionMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        messageQueue_.push(msg);
    }
    queueCV_.notify_one();
}

void TrackerPipeline::processingLoop() {
    LOG_INFO("Pipeline", "Processing loop started");

    while (running_.load()) {
        SPDetectionMessage msg;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait_for(lock, std::chrono::milliseconds(config_.system.cyclePeriodMs),
                              [this] { return !messageQueue_.empty() || !running_.load(); });

            if (!running_.load()) break;
            if (messageQueue_.empty()) continue;

            msg = std::move(messageQueue_.front());
            messageQueue_.pop();
        }

        auto cycleStart = std::chrono::high_resolution_clock::now();

        // Process the dwell through the tracking pipeline
        trackManager_->processDwell(msg);

        // Get track updates and send to display
        auto updates = trackManager_->getTrackUpdates();
        Timestamp ts = msg.timestamp > 0 ? msg.timestamp : nowMicros();

        if (!updates.empty()) {
            sender_->sendTrackUpdates(updates, ts);

            // Log sent tracks
            for (const auto& u : updates) {
                trackManager_->logger().logTrackSent(ts, u);
            }
        }

        ++cycleCount_;

        auto cycleEnd = std::chrono::high_resolution_clock::now();
        auto cycleMs = std::chrono::duration_cast<std::chrono::microseconds>(
                           cycleEnd - cycleStart).count() / 1000.0;

        if (cycleCount_ % 100 == 0) {
            LOG_INFO("Pipeline", "Cycle %lu: %u tracks (%u confirmed), %.2f ms",
                     static_cast<unsigned long>(cycleCount_),
                     trackManager_->numActiveTracks(),
                     trackManager_->numConfirmedTracks(),
                     cycleMs);
        }
    }
}

void TrackerPipeline::printStats() const {
    if (receiver_) {
        LOG_INFO("Pipeline", "Receiver stats: %lu messages, %lu detections",
                 static_cast<unsigned long>(receiver_->totalMessagesReceived()),
                 static_cast<unsigned long>(receiver_->totalDetectionsReceived()));
    }
    if (sender_) {
        LOG_INFO("Pipeline", "Sender stats: %lu messages",
                 static_cast<unsigned long>(sender_->totalMessagesSent()));
    }
    if (trackManager_) {
        LOG_INFO("Pipeline", "Final tracks: %u active, %u confirmed",
                 trackManager_->numActiveTracks(),
                 trackManager_->numConfirmedTracks());
    }
}

} // namespace cuas
