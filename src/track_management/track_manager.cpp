#include "track_management/track_manager.h"
#include "common/logger.h"
#include "common/config.h"
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

    for (int i = 0; i < MEAS_DIM; ++i)
        for (int j = 0; j < MEAS_DIM; ++j)
            measurementNoise_[i][j] = (i == j) ? 625.0 : 0.0;

    if (cfg.system.logEnabled)
        logger_.open(cfg.system.logDirectory, "tracker", getRunInfoString(cfg));

    LOG_INFO("TrackManager", "Initialized. Cluster: %s, Association: %s",
             clusterEngine_->activeMethod().c_str(),
             associationEngine_->activeMethod().c_str());
}

void TrackManager::processDwell(const SPDetectionMessage& msg) {
    Timestamp ts = msg.timestamp > 0 ? msg.timestamp : nowMicros();
    dwellCount_ = msg.dwellCount;

    LOG_DEBUG("TrackManager", "=== Dwell %u: %u detections ===",
              dwellCount_, msg.numDetections);

    logger_.logRawDetections(ts, msg);

    auto filtered = preprocessor_->process(msg.detections);
    logger_.logPreprocessed(ts, filtered);
    LOG_DEBUG("TrackManager", "After preprocessing: %zu detections", filtered.size());

    auto clusters = clusterEngine_->process(filtered);
    logger_.logClustered(ts, clusters);

    // Convert internal Cluster → IDL ClusterData for DDS forwarding.
    lastClusters_.clear();
    lastClusters_.reserve(clusters.size());
    for (const auto& c : clusters) {
        CounterUAS::ClusterData cd;
        cd.clusterId(c.clusterId);
        cd.numDetections(c.numDetections);
        cd.range(c.range);   cd.azimuth(c.azimuth);   cd.elevation(c.elevation);
        cd.strength(c.strength); cd.snr(c.snr);        cd.rcs(c.rcs);
        cd.microDoppler(c.microDoppler);
        cd.x(c.cartesian.x); cd.y(c.cartesian.y);     cd.z(c.cartesian.z);
        lastClusters_.push_back(cd);
    }
    LOG_DEBUG("TrackManager", "After clustering: %zu clusters", clusters.size());

    double dt = 0.0;
    if (lastDwellTime_ > 0) {
        dt = (ts - lastDwellTime_) * 1e-6;
    } else {
        dt = config_.system.cyclePeriodMs * 1e-3;
    }
    if (dt <= 0.0 || dt > 10.0) dt = config_.system.cyclePeriodMs * 1e-3;

    predict(dt);
    associate(clusters);
    maintainTracks();
    deleteTracks();
    classifyTracks();

    lastDwellTime_ = ts;

    LOG_DEBUG("TrackManager", "Active tracks: %u, Confirmed: %u",
              numActiveTracks(), numConfirmedTracks());
}

void TrackManager::predict(double dt) {
    lastPredicted_.clear();

    for (auto& track : tracks_) {
        if (track->status() == TrackStatusVal::Deleted) continue;

        immFilter_->predict(dt, track->immState());
        track->incrementAge();

        Timestamp now = nowMicros();
        logger_.logPredicted(now, track->id(), track->state());

        // Convert predicted state → IDL PredictedEntry for DDS forwarding.
        CounterUAS::PredictedEntry pe;
        pe.trackId(track->id());
        pe.trackStatus(track->status());
        const auto& s = track->state();
        pe.x(s[0]); pe.vx(s[1]); pe.ax(s[2]);
        pe.y(s[3]); pe.vy(s[4]); pe.ay(s[5]);
        pe.z(s[6]); pe.vz(s[7]); pe.az(s[8]);
        auto sph = track->sphericalPosition();
        pe.range(sph.range); pe.azimuth(sph.azimuth); pe.elevation(sph.elevation);
        const auto& P = track->covariance();
        pe.covX(P[0][0]); pe.covY(P[3][3]); pe.covZ(P[6][6]);
        const auto& probs = track->immState().modeProbabilities;
        pe.modelProb0(probs[0]); pe.modelProb1(probs[1]); pe.modelProb2(probs[2]);
        pe.modelProb3(probs[3]); pe.modelProb4(probs[4]);
        lastPredicted_.push_back(pe);

        LOG_TRACE("TrackManager", "Predicted track %u: x=%.1f y=%.1f z=%.1f",
                  track->id(), s[0], s[3], s[6]);
    }
}

void TrackManager::associate(const std::vector<Cluster>& clusters) {
    std::vector<Track*> activeTracks;
    std::vector<int>    activeIndices;
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i) {
        if (tracks_[i]->status() != TrackStatusVal::Deleted) {
            activeTracks.push_back(tracks_[i].get());
            activeIndices.push_back(i);
        }
    }

    std::vector<Track> trackRefs;
    trackRefs.reserve(activeTracks.size());
    for (auto* t : activeTracks)
        trackRefs.push_back(*t);

    // Adaptive measurement noise: range σ = 10 m, angle σ = 0.005 rad.
    {
        static constexpr double SIGMA_RANGE = 10.0;
        static constexpr double SIGMA_ANGLE = 0.005;
        double maxRange = 5000.0;
        for (const auto& t : trackRefs)
            maxRange = std::max(maxRange, t.sphericalPosition().range);
        double sigCross = SIGMA_ANGLE * maxRange;
        measurementNoise_ = {};
        measurementNoise_[0][0] = SIGMA_RANGE * SIGMA_RANGE;
        measurementNoise_[1][1] = sigCross * sigCross;
        measurementNoise_[2][2] = sigCross * sigCross;
    }

    auto assocResult = associationEngine_->process(trackRefs, clusters,
                                                    *immFilter_, measurementNoise_);

    // Convert association results → IDL AssocEntry for DDS forwarding.
    lastAssoc_.clear();
    for (const auto& match : assocResult.matched) {
        CounterUAS::AssocEntry ae;
        ae.trackId(activeTracks[match.trackIndex]->id());
        ae.clusterId(clusters[match.clusterIndex].clusterId);
        ae.distance(match.distance);
        ae.matched(true);
        lastAssoc_.push_back(ae);
    }
    for (int unmIdx : assocResult.unmatchedTracks) {
        CounterUAS::AssocEntry ae;
        ae.trackId(activeTracks[unmIdx]->id());
        ae.clusterId(0xFFFFFFFFu);
        ae.distance(-1.0);
        ae.matched(false);
        lastAssoc_.push_back(ae);
    }
    for (int cIdx : assocResult.unmatchedClusters) {
        CounterUAS::AssocEntry ae;
        ae.trackId(0xFFFFFFFFu);
        ae.clusterId(clusters[cIdx].clusterId);
        ae.distance(-1.0);
        ae.matched(false);
        lastAssoc_.push_back(ae);
    }

    // Update matched tracks.
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

    for (int unmIdx : assocResult.unmatchedTracks) {
        int origIdx = activeIndices[unmIdx];
        tracks_[origIdx]->recordMiss();
        LOG_TRACE("TrackManager", "Track %u missed", tracks_[origIdx]->id());
    }

    // Initiate new tracks from unmatched clusters.
    std::vector<Cluster> unmatchedClusters;
    for (int cIdx : assocResult.unmatchedClusters)
        unmatchedClusters.push_back(clusters[cIdx]);

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
        if (track->status() == TrackStatusVal::Deleted) continue;

        double q = track->quality();
        if (track->consecutiveMisses() == 0)
            q = std::min(1.0, q + maint.qualityBoost);
        else
            q *= maint.qualityDecayRate;
        track->setQuality(q);

        if (track->status() == TrackStatusVal::Tentative) {
            if (track->hitCount() >= static_cast<uint32_t>(maint.confirmHits)) {
                track->setStatus(TrackStatusVal::Confirmed);
                LOG_INFO("TrackManager", "Track %u confirmed (hits=%u)",
                         track->id(), track->hitCount());
            }
        } else if (track->status() == TrackStatusVal::Confirmed) {
            if (track->consecutiveMisses() > 0) {
                track->setStatus(TrackStatusVal::Coasting);
                LOG_DEBUG("TrackManager", "Track %u coasting (misses=%u)",
                          track->id(), track->consecutiveMisses());
            }
        } else if (track->status() == TrackStatusVal::Coasting) {
            if (track->consecutiveMisses() == 0)
                track->setStatus(TrackStatusVal::Confirmed);
        }
    }
}

void TrackManager::deleteTracks() {
    const auto& del = config_.trackManagement.deletion;

    for (auto& track : tracks_) {
        if (track->status() == TrackStatusVal::Deleted) continue;

        bool shouldDelete = false;
        std::string reason;

        if (static_cast<int>(track->consecutiveMisses()) >= del.maxCoastingDwells) {
            shouldDelete = true;  reason = "max_coasting";
        } else if (track->quality() < del.minQuality) {
            shouldDelete = true;  reason = "low_quality";
        } else {
            if (track->sphericalPosition().range > del.maxRange) {
                shouldDelete = true;  reason = "out_of_range";
            }
        }

        if (shouldDelete) {
            track->setStatus(TrackStatusVal::Deleted);
            logger_.logTrackDeleted(nowMicros(), track->id());
            LOG_INFO("TrackManager", "Track %u deleted (%s)",
                     track->id(), reason.c_str());
        }
    }

    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [](const std::unique_ptr<Track>& t) {
                           return t->status() == TrackStatusVal::Deleted;
                       }),
        tracks_.end());
}

void TrackManager::classifyTracks() {
    for (auto& track : tracks_) {
        if (track->status() == TrackStatusVal::Deleted) continue;

        auto vel = track->velocity();
        double speed = std::sqrt(vel.x*vel.x + vel.y*vel.y + vel.z*vel.z);

        const auto& probs = track->immState().modeProbabilities;
        double cvProb  = probs[0];
        double caProb  = probs[1] + probs[2];
        double ctrProb = probs[3] + probs[4];

        if (speed < 2.0)
            track->setClassification(TrackClassVal::Clutter);
        else if (ctrProb > 0.4 && speed > 5.0 && speed < 30.0)
            track->setClassification(TrackClassVal::DroneRotary);
        else if (cvProb > 0.3 && speed > 15.0 && speed < 80.0)
            track->setClassification(TrackClassVal::DroneFixedWing);
        else if (speed > 5.0 && speed < 25.0 && caProb > 0.3)
            track->setClassification(TrackClassVal::Bird);
        else
            track->setClassification(TrackClassVal::Unknown);
    }
}

std::vector<CounterUAS::TrackUpdateMessage> TrackManager::getTrackUpdates() const {
    std::vector<CounterUAS::TrackUpdateMessage> updates;
    updates.reserve(tracks_.size());
    for (const auto& track : tracks_)
        updates.push_back(track->toUpdateMessage());
    return updates;
}

uint32_t TrackManager::numActiveTracks() const {
    uint32_t count = 0;
    for (const auto& t : tracks_)
        if (t->status() != TrackStatusVal::Deleted) ++count;
    return count;
}

uint32_t TrackManager::numConfirmedTracks() const {
    uint32_t count = 0;
    for (const auto& t : tracks_)
        if (t->status() == TrackStatusVal::Confirmed) ++count;
    return count;
}

} // namespace cuas
