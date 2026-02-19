/*
 * DSP Data Injector Simulator
 *
 * Generates synthetic SP detection messages simulating drone targets
 * and sends them over UDP to the tracker at configurable rates.
 *
 * Usage: dsp_injector [tracker_ip] [tracker_port] [num_targets] [duration_sec]
 */

#include "common/types.h"
#include "common/udp_socket.h"
#include "common/constants.h"
#include "common/logger.h"

#include <iostream>
#include <cmath>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

void signalHandler(int) { g_running.store(false); }

struct SimTarget {
    double range;
    double azimuth;
    double elevation;
    double speed;
    double heading;       // radians
    double climbRate;     // m/s
    double turnRate;      // rad/s
    double rcs;           // dBsm
    double microDoppler;  // Hz
    bool   active;
};

class DSPSimulator {
public:
    DSPSimulator(int numTargets, double noiseFloor, unsigned seed)
        : rng_(seed), noiseFloor_(noiseFloor) {
        initTargets(numTargets);
    }

    void initTargets(int n) {
        std::uniform_real_distribution<> rangeDist(500.0, 8000.0);
        std::uniform_real_distribution<> azDist(-1.5, 1.5);
        std::uniform_real_distribution<> elDist(0.02, 0.5);
        std::uniform_real_distribution<> speedDist(5.0, 40.0);
        std::uniform_real_distribution<> headingDist(-cuas::PI, cuas::PI);
        std::uniform_real_distribution<> turnDist(-0.05, 0.05);
        std::uniform_real_distribution<> rcsDist(-15.0, 5.0);
        std::uniform_real_distribution<> microDist(50.0, 500.0);

        targets_.clear();
        for (int i = 0; i < n; ++i) {
            SimTarget t;
            t.range       = rangeDist(rng_);
            t.azimuth     = azDist(rng_);
            t.elevation   = elDist(rng_);
            t.speed       = speedDist(rng_);
            t.heading     = headingDist(rng_);
            t.climbRate   = 0.5;
            t.turnRate    = turnDist(rng_);
            t.rcs         = rcsDist(rng_);
            t.microDoppler = microDist(rng_);
            t.active      = true;
            targets_.push_back(t);
        }
    }

    void updateTargets(double dt) {
        std::normal_distribution<> accelNoise(0.0, 0.5);
        std::normal_distribution<> turnNoise(0.0, 0.005);

        for (auto& t : targets_) {
            if (!t.active) continue;

            // Convert to Cartesian, update, convert back
            double x = t.range * std::cos(t.elevation) * std::cos(t.azimuth);
            double y = t.range * std::cos(t.elevation) * std::sin(t.azimuth);
            double z = t.range * std::sin(t.elevation);

            double vx = t.speed * std::cos(t.heading);
            double vy = t.speed * std::sin(t.heading);
            double vz = t.climbRate;

            x += vx * dt;
            y += vy * dt;
            z += vz * dt;

            t.heading  += t.turnRate * dt + turnNoise(rng_) * dt;
            t.speed    += accelNoise(rng_) * dt;
            t.climbRate += accelNoise(rng_) * 0.1 * dt;

            if (t.speed < 2.0) t.speed = 2.0;
            if (t.speed > 60.0) t.speed = 60.0;
            if (z < 10.0) { z = 10.0; t.climbRate = std::abs(t.climbRate); }
            if (z > 3000.0) { t.climbRate = -std::abs(t.climbRate); }

            t.range     = std::sqrt(x * x + y * y + z * z);
            t.azimuth   = std::atan2(y, x);
            t.elevation = std::asin(z / std::max(t.range, 1.0));

            if (t.range > 20000.0 || t.range < 30.0) {
                t.active = false;
            }
        }
    }

    cuas::SPDetectionMessage generateDwell(uint32_t dwellCount) {
        cuas::SPDetectionMessage msg;
        msg.messageId   = cuas::MSG_ID_SP_DETECTION;
        msg.dwellCount  = dwellCount;
        msg.timestamp   = cuas::nowMicros();

        std::normal_distribution<> rangeNoise(0.0, 10.0);
        std::normal_distribution<> azNoise(0.0, 0.005);
        std::normal_distribution<> elNoise(0.0, 0.005);
        std::normal_distribution<> strNoise(0.0, 3.0);
        std::uniform_int_distribution<> numFalseAlarms(0, 3);
        std::uniform_real_distribution<> probDetect(0.0, 1.0);

        // Target returns
        for (const auto& t : targets_) {
            if (!t.active) continue;

            // Detection probability depends on range and RCS
            double pd = 0.95 - (t.range / 50000.0);
            if (probDetect(rng_) > pd) continue;

            cuas::Detection det;
            det.range      = t.range + rangeNoise(rng_);
            det.azimuth    = t.azimuth + azNoise(rng_);
            det.elevation  = t.elevation + elNoise(rng_);
            det.rcs        = t.rcs + strNoise(rng_) * 0.5;
            det.microDoppler = t.microDoppler + strNoise(rng_) * 10.0;

            double pathLoss = 40.0 * std::log10(std::max(det.range, 1.0));
            det.strength   = -30.0 + det.rcs - pathLoss + 100.0 + strNoise(rng_);
            det.noise      = noiseFloor_ + strNoise(rng_) * 0.5;
            det.snr        = det.strength - det.noise;

            // Multiple detections per target (radar sidelobes, multipath)
            int numDets = 1;
            std::uniform_int_distribution<> extraDets(0, 2);
            numDets += extraDets(rng_);

            for (int d = 0; d < numDets; ++d) {
                cuas::Detection extra = det;
                if (d > 0) {
                    extra.range     += rangeNoise(rng_) * 2.0;
                    extra.azimuth   += azNoise(rng_) * 2.0;
                    extra.elevation += elNoise(rng_) * 2.0;
                    extra.strength  -= 3.0 + std::abs(strNoise(rng_));
                    extra.snr        = extra.strength - extra.noise;
                }
                msg.detections.push_back(extra);
            }
        }

        // False alarms (clutter)
        int nFA = numFalseAlarms(rng_);
        std::uniform_real_distribution<> faRange(100.0, 15000.0);
        std::uniform_real_distribution<> faAz(-2.0, 2.0);
        std::uniform_real_distribution<> faEl(0.0, 0.3);

        for (int i = 0; i < nFA; ++i) {
            cuas::Detection fa;
            fa.range      = faRange(rng_);
            fa.azimuth    = faAz(rng_);
            fa.elevation  = faEl(rng_);
            fa.strength   = noiseFloor_ + 5.0 + strNoise(rng_);
            fa.noise      = noiseFloor_;
            fa.snr        = fa.strength - fa.noise;
            fa.rcs        = -20.0 + strNoise(rng_);
            fa.microDoppler = strNoise(rng_) * 5.0;
            msg.detections.push_back(fa);
        }

        msg.numDetections = static_cast<uint32_t>(msg.detections.size());
        return msg;
    }

    int activeTargets() const {
        int count = 0;
        for (const auto& t : targets_)
            if (t.active) ++count;
        return count;
    }

private:
    std::mt19937 rng_;
    double noiseFloor_;
    std::vector<SimTarget> targets_;
};

int main(int argc, char* argv[]) {
    std::string trackerIp   = "127.0.0.1";
    int         trackerPort = 50000;
    int         numTargets  = 5;
    int         durationSec = 60;
    int         rateMs      = 100;

    if (argc > 1) trackerIp   = argv[1];
    if (argc > 2) trackerPort = std::stoi(argv[2]);
    if (argc > 3) numTargets  = std::stoi(argv[3]);
    if (argc > 4) durationSec = std::stoi(argv[4]);
    if (argc > 5) rateMs      = std::stoi(argv[5]);

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
#endif

    cuas::ConsoleLogger::instance().setLevel(cuas::ConsoleLogger::INFO);

    std::cerr <<
        "================================================================\n"
        "  DSP Data Injector Simulator\n"
        "  Target: " << trackerIp << ":" << trackerPort << "\n"
        "  Targets: " << numTargets << ", Duration: " << durationSec << "s\n"
        "  Rate: " << rateMs << "ms\n"
        "================================================================\n";

    cuas::UdpSocket::initNetwork();
    cuas::UdpSocket socket;
    socket.setDestination(trackerIp, trackerPort);

    unsigned seed = static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    DSPSimulator sim(numTargets, -90.0, seed);

    double dt = rateMs * 0.001;
    uint32_t dwellCount = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (g_running.load()) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= durationSec)
            break;

        sim.updateTargets(dt);
        auto msg = sim.generateDwell(dwellCount);

        auto data = cuas::MessageSerializer::serialize(msg);
        socket.send(data.data(), static_cast<int>(data.size()));

        if (dwellCount % 50 == 0) {
            LOG_INFO("DSPInjector", "Dwell %u: %u detections, %d active targets",
                     dwellCount, msg.numDetections, sim.activeTargets());
        }

        ++dwellCount;
        std::this_thread::sleep_for(std::chrono::milliseconds(rateMs));
    }

    LOG_INFO("DSPInjector", "Finished. Total dwells: %u", dwellCount);
    cuas::UdpSocket::cleanupNetwork();
    return 0;
}
