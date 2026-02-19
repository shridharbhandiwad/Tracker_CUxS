#include "track_management/track_manager.h"
#include "common/logger.h"
#include "common/constants.h"
#include <algorithm>
#include <cmath>

namespace cuas {

TrackManager::TrackManager(const TrackerConfig& cfg) : config_(cfg) {
    preprocessor_      = std::make_unique<Preprocessor>(cfg.preprocessing);
    clusterEngine_     = std::make_unique<ClusterEngine>(cfg.clustering);
    immFilter_         = std::make_unique<IMMFilter>(cfg.prediction);
    associationEngine_ = std::make_unique<AssociationEngine>(cfg.association);
    trackInitiator_    = std::make_unique<TrackInitiator>(
        cfg.trackManagement.initiation,
        cfg.trackManagement.initialCovariance,
        cfg.prediction);

    // Measurement noise (position uncertainty in Cartesian)
    double sigR = 25.0;
    double posNoise = sigR * sigR;
    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j)
            measurementNoise_[i][j] = (i == j) ? posNoise : 0.0;

    if (cfg.system.logEnabled) {
        logger_.open(cfg.system.logDirectory, "tracker");
    }

    LOG_INFO("TrackManager", "Initialized. Cluster: %s, Association: %s",
             clusterEngine_->activeMethod().c_str(),
             associationEngine_->activeMethod().c_str());
}

void TrackManager::processDwell(const SPDetectionMessage& msg) {
    Timestamp ts = msg.timestamp > 0 ? msg.timestamp : nowMicros();
    dwellCount_ = msg.dwellCount;

    LOG_DEBUG("TrackManager", "=== Dwell %u: %u detections ===",
              dwellCount_, msg.numDetections);

    // 1. Log raw
    logger_.logRawDetections(ts, msg);

    // 2. Preprocess
    auto filtered = preprocessor_->process(msg.detections);
    logger_.logPreprocessed(ts, filtered);

    LOG_DEBUG("TrackManager", "After preprocessing: %zu detections", filtered.size());

    // 3. Cluster
    auto clusters = clusterEngine_->process(filtered);
    logger_.logClustered(ts, clusters);

    LOG_DEBUG("TrackManager", "After clustering: %zu clusters", clusters.size());

    // 4. Predict existing tracks
    double dt = 0.0;
    if (lastDwellTime_ > 0) {
        dt = (ts - lastDwellTime_) * 1e-6; // to seconds
    } else {
        dt = config_.system.cyclePeriodMs * 1e-3;
    }
    if (dt <= 0.0 || dt > 10.0) dt = config_.system.cyclePeriodMs * 1e-3;

    predict(dt);

    // 5. Associate
    associate(clusters);

    // 6. Maintain and delete
    maintainTracks();
    deleteTracks();

    // 7. Classify
    classifyTracks();

    lastDwellTime_ = ts;

    LOG_DEBUG("TrackManager", "Active tracks: %u, Confirmed: %u",
              numActiveTracks(), numConfirmedTracks());
}

void TrackManager::predict(double dt) {
    for (auto& track : tracks_) {
        if (track->status() == TrackStatus::Deleted) continue;

        immFilter_->predict(dt, track->immState());
        track->incrementAge();

        logger_.logPredicted(nowMicros(), track->id(), track->state());

        LOG_TRACE("TrackManager", "Predicted track %u: x=%.1f y=%.1f z=%.1f",
                  track->id(), track->state()[0], track->state()[3], track->state()[6]);
    }
}

void TrackManager::associate(const std::vector<Cluster>& clusters) {
    // Collect active tracks for association
    std::vector<Track*> activeTracks;
    std::vector<int> activeIndices;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        if (tracks_[i]->status() != TrackStatus::Deleted) {
            activeTracks.push_back(tracks_[i].get());
            activeIndices.push_back(i);
        }
    }

    // Build vector of references
    std::vector<Track> trackRefs;
    trackRefs.reserve(activeTracks.size());
    for (auto* t : activeTracks) {
        trackRefs.push_back(*t);
    }

    auto assocResult = associationEngine_->process(trackRefs, clusters,
                                                    *immFilter_, measurementNoise_);

    // Update matched tracks
    for (const auto& match : assocResult.matched) {
        int origIdx = activeIndices[match.trackIndex];
        const auto& cluster = clusters[match.clusterIndex];
        MeasVector z = {cluster.cartesian.x, cluster.cartesian.y, cluster.cartesian.z};

        immFilter_->update(tracks_[origIdx]->immState(), z, measurementNoise_);
        tracks_[origIdx]->recordHit();

        logger_.logAssociated(nowMicros(), tracks_[origIdx]->id(),
                              cluster.clusterId, match.distance);
        logger_.logTrackUpdated(nowMicros(), tracks_[origIdx]->id(),
                                tracks_[origIdx]->state(), tracks_[origIdx]->status());

        LOG_TRACE("TrackManager", "Track %u updated with cluster %u (d=%.2f)",
                  tracks_[origIdx]->id(), cluster.clusterId, match.distance);
    }

    // Handle unmatched tracks
    for (int unmIdx : assocResult.unmatchedTracks) {
        int origIdx = activeIndices[unmIdx];
        tracks_[origIdx]->recordMiss();
        LOG_TRACE("TrackManager", "Track %u missed", tracks_[origIdx]->id());
    }

    // Initiate new tracks from unmatched clusters
    std::vector<Cluster> unmatchedClusters;
    for (int cIdx : assocResult.unmatchedClusters) {
        unmatchedClusters.push_back(clusters[cIdx]);
    }

    if (!unmatchedClusters.empty()) {
        Timestamp ts = nowMicros();
        auto newTracks = trackInitiator_->processCandidates(
            unmatchedClusters, ts, dwellCount_);

        for (auto& nt : newTracks) {
            logger_.logTrackInitiated(ts, nt->id(), nt->state());
            tracks_.push_back(std::move(nt));
        }

        trackInitiator_->purgeStaleCandidates(dwellCount_);
    }
}

void TrackManager::maintainTracks() {
    const auto& maint = config_.trackManagement.maintenance;

    for (auto& track : tracks_) {
        if (track->status() == TrackStatus::Deleted) continue;

        // Quality update
        double q = track->quality();
        if (track->consecutiveMisses() == 0) {
            q = std::min(1.0, q + maint.qualityBoost);
        } else {
            q *= maint.qualityDecayRate;
        }
        track->setQuality(q);

        // Status transitions
        if (track->status() == TrackStatus::Tentative) {
            if (track->hitCount() >= static_cast<uint32_t>(maint.confirmHits)) {
                track->setStatus(TrackStatus::Confirmed);
                LOG_INFO("TrackManager", "Track %u confirmed (hits=%u)",
                         track->id(), track->hitCount());
            }
        } else if (track->status() == TrackStatus::Confirmed) {
            if (track->consecutiveMisses() > 0) {
                track->setStatus(TrackStatus::Coasting);
                LOG_DEBUG("TrackManager", "Track %u coasting (misses=%u)",
                          track->id(), track->consecutiveMisses());
            }
        } else if (track->status() == TrackStatus::Coasting) {
            if (track->consecutiveMisses() == 0) {
                track->setStatus(TrackStatus::Confirmed);
            }
        }
    }
}

void TrackManager::deleteTracks() {
    const auto& del = config_.trackManagement.deletion;

    for (auto& track : tracks_) {
        if (track->status() == TrackStatus::Deleted) continue;

        bool shouldDelete = false;
        std::string reason;

        if (static_cast<int>(track->consecutiveMisses()) >= del.maxCoastingDwells) {
            shouldDelete = true;
            reason = "max_coasting";
        } else if (track->quality() < del.minQuality) {
            shouldDelete = true;
            reason = "low_quality";
        } else {
            auto sph = track->sphericalPosition();
            if (sph.range > del.maxRange) {
                shouldDelete = true;
                reason = "out_of_range";
            }
        }

        if (shouldDelete) {
            track->setStatus(TrackStatus::Deleted);
            logger_.logTrackDeleted(nowMicros(), track->id());
            LOG_INFO("TrackManager", "Track %u deleted (%s)",
                     track->id(), reason.c_str());
        }
    }

    // Remove deleted tracks
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [](const std::unique_ptr<Track>& t) {
                           return t->status() == TrackStatus::Deleted;
                       }),
        tracks_.end());
}

void TrackManager::classifyTracks() {
    for (auto& track : tracks_) {
        if (track->status() == TrackStatus::Deleted) continue;

        auto vel = track->velocity();
        double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

        // Simple heuristic classification based on speed and IMM mode probabilities
        const auto& probs = track->immState().modeProbabilities;
        double cvProb  = probs[0];
        double caProb  = probs[1] + probs[2];
        double ctrProb = probs[3] + probs[4];

        if (speed < 2.0) {
            track->setClassification(TrackClassification::Clutter);
        } else if (ctrProb > 0.4 && speed > 5.0 && speed < 30.0) {
            track->setClassification(TrackClassification::DroneRotary);
        } else if (cvProb > 0.3 && speed > 15.0 && speed < 80.0) {
            track->setClassification(TrackClassification::DroneFixedWing);
        } else if (speed > 5.0 && speed < 25.0 && caProb > 0.3) {
            track->setClassification(TrackClassification::Bird);
        } else {
            track->setClassification(TrackClassification::Unknown);
        }
    }
}

std::vector<TrackUpdateMessage> TrackManager::getTrackUpdates() const {
    std::vector<TrackUpdateMessage> updates;
    updates.reserve(tracks_.size());
    for (const auto& track : tracks_) {
        updates.push_back(track->toUpdateMessage());
    }
    return updates;
}

uint32_t TrackManager::numActiveTracks() const {
    uint32_t count = 0;
    for (const auto& t : tracks_)
        if (t->status() != TrackStatus::Deleted) ++count;
    return count;
}

uint32_t TrackManager::numConfirmedTracks() const {
    uint32_t count = 0;
    for (const auto& t : tracks_)
        if (t->status() == TrackStatus::Confirmed) ++count;
    return count;
}

} // namespace cuas
