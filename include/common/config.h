#pragma once

#include "types.h"
#include "constants.h"
#include <string>
#include <vector>
#include <array>

namespace cuas {

struct SystemConfig {
    int    cyclePeriodMs       = 100;
    int    maxDetectionsPerDwell = 256;
    int    maxTracks           = 200;
    std::string logDirectory   = "./logs";
    bool   logEnabled          = true;
    int    logLevel            = 3;
};

struct NetworkConfig {
    std::string receiverIp     = "0.0.0.0";
    int    receiverPort        = 50000;
    std::string senderIp       = "127.0.0.1";
    int    senderPort          = 50001;
    int    receiveBufferSize   = 65536;
    int    sendBufferSize      = 65536;
};

struct PreprocessConfig {
    double minRange     = 50.0;
    double maxRange     = 20000.0;
    double minAzimuth   = -PI;
    double maxAzimuth   = PI;
    double minElevation = -0.1745;
    double maxElevation = 1.5708;
    double minSNR       = 8.0;
    double maxSNR       = 60.0;
    double minRCS       = -30.0;
    double maxRCS       = 20.0;
    double minStrength  = -100.0;
    double maxStrength  = 0.0;
};

struct DBScanConfig {
    double epsilonRange     = 50.0;
    double epsilonAzimuth   = 0.02;
    double epsilonElevation = 0.02;
    int    minPoints        = 2;
};

struct RangeBasedConfig {
    double rangeGateSize     = 75.0;
    double azimuthGateSize   = 0.03;
    double elevationGateSize = 0.03;
};

struct RangeStrengthConfig {
    double rangeGateSize     = 75.0;
    double azimuthGateSize   = 0.03;
    double elevationGateSize = 0.03;
    double strengthGateSize  = 6.0;
};

struct ClusterConfig {
    ClusterMethod method = ClusterMethod::DBSCAN;
    DBScanConfig dbscan;
    RangeBasedConfig rangeBased;
    RangeStrengthConfig rangeStrength;
};

struct IMMConfig {
    int numModels = 5;
    std::array<double, IMM_NUM_MODELS> initialModeProbabilities = {0.4, 0.15, 0.15, 0.15, 0.15};
    std::array<std::array<double, IMM_NUM_MODELS>, IMM_NUM_MODELS> transitionMatrix;
};

struct CVConfig {
    double processNoiseStd = 1.0;
};

struct CAConfig {
    double processNoiseStd = 2.0;
    double accelDecayRate  = 0.95;
};

struct CTRConfig {
    double processNoiseStd  = 1.5;
    double turnRateNoiseStd = 0.05;
};

struct PredictionConfig {
    IMMConfig imm;
    CVConfig  cv;
    CAConfig  ca1;
    CAConfig  ca2;
    CTRConfig ctr1;
    CTRConfig ctr2;
};

struct MahalanobisConfig {
    double distanceThreshold = 9.21;
};

struct GNNConfig {
    double costThreshold = 16.0;
};

struct JPDAConfig {
    double gateSize             = 16.0;
    double clutterDensity       = 1e-6;
    double detectionProbability = 0.9;
};

struct AssociationConfig {
    AssociationMethod method = AssociationMethod::GNN;
    double gatingThreshold  = 16.0;
    MahalanobisConfig mahalanobis;
    GNNConfig gnn;
    JPDAConfig jpda;
};

struct InitiationConfig {
    std::string method        = "mOfN";
    int  m                    = 3;
    int  n                    = 5;
    double maxInitiationRange = 15000.0;
    double velocityGate       = 100.0;
};

struct MaintenanceConfig {
    int    confirmHits       = 5;
    int    coastingLimit     = 10;
    int    deleteAfterMisses = 15;
    double qualityDecayRate  = 0.95;
    double qualityBoost      = 0.1;
    double minQualityThreshold = 0.1;
};

struct DeletionConfig {
    int    maxCoastingDwells = 15;
    double minQuality       = 0.05;
    double maxRange         = 25000.0;
};

struct InitialCovarianceConfig {
    double positionStd     = 50.0;
    double velocityStd     = 20.0;
    double accelerationStd = 5.0;
};

struct TrackManagementConfig {
    InitiationConfig       initiation;
    MaintenanceConfig      maintenance;
    DeletionConfig         deletion;
    InitialCovarianceConfig initialCovariance;
};

struct DisplayConfig {
    int  updateRateMs     = 200;
    bool sendDeletedTracks = true;
};

struct TrackerConfig {
    SystemConfig          system;
    NetworkConfig         network;
    PreprocessConfig      preprocessing;
    ClusterConfig         clustering;
    PredictionConfig      prediction;
    AssociationConfig     association;
    TrackManagementConfig trackManagement;
    DisplayConfig         display;
};

TrackerConfig loadConfig(const std::string& filepath);

} // namespace cuas
