#pragma once

#include "common/config.h"
#include "receiver/detection_receiver.h"
#include "track_management/track_manager.h"
#include "sender/track_sender.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>

namespace cuas {

class TrackerPipeline {
public:
    explicit TrackerPipeline(const TrackerConfig& cfg);
    ~TrackerPipeline();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    void printStats() const;

private:
    void processingLoop();
    void onDetectionReceived(const SPDetectionMessage& msg);

    TrackerConfig config_;
    std::unique_ptr<DetectionReceiver> receiver_;
    std::unique_ptr<TrackManager>      trackManager_;
    std::unique_ptr<TrackSender>       sender_;

    std::queue<SPDetectionMessage> messageQueue_;
    std::mutex                     queueMutex_;
    std::condition_variable        queueCV_;
    std::atomic<bool>              running_{false};
    std::thread                    processingThread_;

    uint64_t cycleCount_ = 0;
};

} // namespace cuas
