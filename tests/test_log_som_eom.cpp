/*
 * test_log_som_eom.cpp
 *
 * Self-contained integration test for the SOM/EOM binary log framing.
 *
 * Wire format under test (little-endian):
 *
 *   ┌─────────────────────────────────────────────────────┐
 *   │  SOM   magic       (4 B)  = 0xCAFEBABE              │
 *   │        recordType  (4 B)  LogRecordType enum value  │
 *   │        timestamp   (8 B)  microseconds since epoch  │
 *   │        payloadSize (4 B)  bytes that follow          │
 *   ├─────────────────────────────────────────────────────┤
 *   │  PAYLOAD           (payloadSize bytes)               │
 *   ├─────────────────────────────────────────────────────┤
 *   │  EOM   eom         (4 B)  = 0xDEADBEEF              │
 *   └─────────────────────────────────────────────────────┘
 *
 * Tests
 *   1. Framing constant values
 *   2. Valid SOM/EOM roundtrip with diverse sample records
 *   3. Corrupted EOM detection
 *   4. Resync recovery after a bad-EOM record
 *   5. Zero-payload record (EOM directly after header)
 *   6. BinaryLogger write path (uses the actual logger API)
 */

#include "common/types.h"
#include "common/logger.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Lightweight test framework
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr, label)                                              \
    do {                                                                \
        if (expr) {                                                     \
            std::cout << "  PASS  " << (label) << "\n";                \
            ++g_pass;                                                   \
        } else {                                                        \
            std::cout << "  FAIL  " << (label) << "\n";                \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers: low-level record writers used by the tests
// ---------------------------------------------------------------------------

// Write one correctly framed record.
static void writeRecord(std::ofstream& out,
                        uint32_t recType, uint64_t ts,
                        const void* data, uint32_t size)
{
    cuas::LogRecordHeader hdr{};
    hdr.magic       = cuas::LOG_MAGIC;
    hdr.recordType  = recType;
    hdr.timestamp   = ts;
    hdr.payloadSize = size;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (size > 0 && data)
        out.write(reinterpret_cast<const char*>(data), size);
    const uint32_t eom = cuas::LOG_EOM;
    out.write(reinterpret_cast<const char*>(&eom), sizeof(eom));
}

// Write one record with a deliberately corrupted EOM.
static void writeRecordBadEOM(std::ofstream& out,
                               uint32_t recType, uint64_t ts,
                               const void* data, uint32_t size)
{
    cuas::LogRecordHeader hdr{};
    hdr.magic       = cuas::LOG_MAGIC;
    hdr.recordType  = recType;
    hdr.timestamp   = ts;
    hdr.payloadSize = size;
    out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (size > 0 && data)
        out.write(reinterpret_cast<const char*>(data), size);
    const uint32_t badEom = 0x00000000u;          // wrong sentinel
    out.write(reinterpret_cast<const char*>(&badEom), sizeof(badEom));
}

// ---------------------------------------------------------------------------
// Test 1 — Framing constant values
// ---------------------------------------------------------------------------
static void testFramingConstants()
{
    std::cout << "\n[Test 1] Framing constant values\n";

    CHECK(cuas::LOG_MAGIC == 0xCAFEBABEu,
          "LOG_MAGIC == 0xCAFEBABE  (SOM sentinel)");
    CHECK(cuas::LOG_EOM   == 0xDEADBEEFu,
          "LOG_EOM   == 0xDEADBEEF  (EOM sentinel)");
    CHECK(cuas::LOG_MAGIC != cuas::LOG_EOM,
          "SOM and EOM are distinct values");
    CHECK(sizeof(cuas::LogRecordHeader) == 20u,
          "LogRecordHeader is exactly 20 bytes (4+4+8+4)");
    CHECK(cuas::LOG_MAX_PAYLOAD == 64u * 1024u * 1024u,
          "LOG_MAX_PAYLOAD == 64 MiB");
}

// ---------------------------------------------------------------------------
// Test 2 — Valid SOM/EOM roundtrip with diverse sample data
// ---------------------------------------------------------------------------
static void testValidRoundtrip(const std::string& path)
{
    std::cout << "\n[Test 2] Valid SOM/EOM roundtrip with sample data\n";

    // ---- Write phase -------------------------------------------------------
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        // Record A: RunInfo (text payload)
        const std::string runInfo =
            "Clusterer=DBSCAN Association=GNN Models=CV+CA+CTR";
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::RunInfo),
                    0,
                    runInfo.data(),
                    static_cast<uint32_t>(runInfo.size()));

        // Record B: RawDetection
        // Payload layout: msgId(4) + dwellCount(4) + timestamp(8) + numDets(4) + Detection(64)
        cuas::Detection det{};
        det.range     = 1500.0;
        det.azimuth   = 0.5236;   // ~30 deg
        det.elevation = 0.1745;   // ~10 deg
        det.strength  = -45.0;
        det.snr       =  12.5;
        det.rcs       =   0.1;
        det.microDoppler = 25.0;

        std::vector<uint8_t> rawBuf;
        auto append = [&rawBuf](const void* src, size_t n) {
            const auto* b = reinterpret_cast<const uint8_t*>(src);
            rawBuf.insert(rawBuf.end(), b, b + n);
        };
        const uint32_t msgId      = 0x0001u;
        const uint32_t dwellCount = 42u;
        const uint32_t numDets    = 1u;
        const uint64_t detTs      = 1'700'000'000'000'000ULL;
        append(&msgId,      4);
        append(&dwellCount, 4);
        append(&detTs,      8);
        append(&numDets,    4);
        append(&det, sizeof(det));
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::RawDetection),
                    detTs,
                    rawBuf.data(),
                    static_cast<uint32_t>(rawBuf.size()));

        // Record C: TrackInitiated
        const uint32_t trackId = 7u;
        cuas::StateVector sv{};
        sv[0] =  1200.0;  sv[1] =  15.0;  sv[2] = 0.0;   // x, vx, ax
        sv[3] =  -300.0;  sv[4] =  -5.0;  sv[5] = 0.0;   // y, vy, ay
        sv[6] =    80.0;  sv[7] =   2.0;  sv[8] = 0.0;   // z, vz, az

        std::vector<uint8_t> initBuf(sizeof(uint32_t) + cuas::STATE_DIM * sizeof(double));
        uint8_t* p = initBuf.data();
        std::memcpy(p, &trackId, 4); p += 4;
        for (int i = 0; i < cuas::STATE_DIM; ++i) {
            std::memcpy(p, &sv[i], 8); p += 8;
        }
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::TrackInitiated),
                    detTs + 1000u,
                    initBuf.data(),
                    static_cast<uint32_t>(initBuf.size()));

        // Record D: TrackDeleted (small 4-byte payload)
        const uint32_t deletedId = 7u;
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::TrackDeleted),
                    detTs + 5000u,
                    &deletedId,
                    sizeof(deletedId));
    }

    // ---- Read & verify phase -----------------------------------------------
    std::ifstream in(path, std::ios::binary);

    // Record A: RunInfo
    {
        cuas::LogRecordHeader hdr{};
        bool hok = cuas::BinaryLogger::readHeader(in, hdr);
        CHECK(hok,                                         "RunInfo: readHeader OK");
        CHECK(hdr.magic == cuas::LOG_MAGIC,                "RunInfo: SOM magic correct");
        CHECK(hdr.recordType ==
              static_cast<uint32_t>(cuas::LogRecordType::RunInfo),
                                                           "RunInfo: record type correct");
        CHECK(hdr.timestamp == 0,                          "RunInfo: timestamp == 0");

        std::vector<uint8_t> payload;
        bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
        CHECK(pok,                                         "RunInfo: readPayload OK (EOM valid)");
        std::string s(payload.begin(), payload.end());
        CHECK(s.find("DBSCAN") != std::string::npos,       "RunInfo: payload content correct");
    }

    // Record B: RawDetection
    {
        cuas::LogRecordHeader hdr{};
        bool hok = cuas::BinaryLogger::readHeader(in, hdr);
        CHECK(hok,                                         "RawDetection: readHeader OK");
        CHECK(hdr.magic == cuas::LOG_MAGIC,                "RawDetection: SOM magic correct");
        CHECK(hdr.recordType ==
              static_cast<uint32_t>(cuas::LogRecordType::RawDetection),
                                                           "RawDetection: record type correct");

        std::vector<uint8_t> payload;
        bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
        CHECK(pok,                                         "RawDetection: readPayload OK (EOM valid)");

        if (payload.size() >= 20) {
            uint32_t dc = 0, nd = 0;
            std::memcpy(&dc, payload.data() + 4,  4);
            std::memcpy(&nd, payload.data() + 16, 4);
            CHECK(dc == 42u, "RawDetection: dwellCount == 42");
            CHECK(nd == 1u,  "RawDetection: numDetections == 1");
        }
        if (payload.size() >= 20u + sizeof(cuas::Detection)) {
            cuas::Detection d{};
            std::memcpy(&d, payload.data() + 20, sizeof(d));
            CHECK(d.range > 1499.9 && d.range < 1500.1,
                  "RawDetection: detection.range == 1500 m");
            CHECK(d.snr > 12.4 && d.snr < 12.6,
                  "RawDetection: detection.snr == 12.5 dB");
        }
    }

    // Record C: TrackInitiated
    {
        cuas::LogRecordHeader hdr{};
        bool hok = cuas::BinaryLogger::readHeader(in, hdr);
        CHECK(hok, "TrackInitiated: readHeader OK");
        CHECK(hdr.recordType ==
              static_cast<uint32_t>(cuas::LogRecordType::TrackInitiated),
                                                           "TrackInitiated: record type correct");

        std::vector<uint8_t> payload;
        bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
        CHECK(pok, "TrackInitiated: readPayload OK (EOM valid)");

        if (payload.size() >= 4u) {
            uint32_t tid = 0;
            std::memcpy(&tid, payload.data(), 4);
            CHECK(tid == 7u, "TrackInitiated: trackId == 7");
        }
        if (payload.size() >= 4u + cuas::STATE_DIM * sizeof(double)) {
            double x = 0.0;
            std::memcpy(&x, payload.data() + 4, 8);   // sv[0] = x
            CHECK(x > 1199.9 && x < 1200.1,
                  "TrackInitiated: state[x] == 1200 m");
        }
    }

    // Record D: TrackDeleted
    {
        cuas::LogRecordHeader hdr{};
        bool hok = cuas::BinaryLogger::readHeader(in, hdr);
        CHECK(hok, "TrackDeleted: readHeader OK");
        CHECK(hdr.recordType ==
              static_cast<uint32_t>(cuas::LogRecordType::TrackDeleted),
                                                           "TrackDeleted: record type correct");

        std::vector<uint8_t> payload;
        bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
        CHECK(pok, "TrackDeleted: readPayload OK (EOM valid)");

        if (payload.size() >= 4u) {
            uint32_t tid = 0;
            std::memcpy(&tid, payload.data(), 4);
            CHECK(tid == 7u, "TrackDeleted: trackId == 7");
        }
    }

    // Must be at EOF now
    {
        cuas::LogRecordHeader hdr{};
        bool ok = cuas::BinaryLogger::readHeader(in, hdr);
        CHECK(!ok, "readHeader returns false at expected EOF");
    }
}

// ---------------------------------------------------------------------------
// Test 3 — Corrupted EOM detection
// ---------------------------------------------------------------------------
static void testCorruptedEOM(const std::string& path)
{
    std::cout << "\n[Test 3] Corrupted EOM detection\n";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const uint32_t trackId = 99u;
        writeRecordBadEOM(out,
                          static_cast<uint32_t>(cuas::LogRecordType::TrackDeleted),
                          12'345'678'000'000ULL,
                          &trackId,
                          sizeof(trackId));
    }

    std::ifstream in(path, std::ios::binary);
    cuas::LogRecordHeader hdr{};
    bool hok = cuas::BinaryLogger::readHeader(in, hdr);
    CHECK(hok,                         "Corrupted EOM: readHeader succeeds (SOM intact)");
    CHECK(hdr.magic == cuas::LOG_MAGIC, "Corrupted EOM: SOM magic is correct");

    std::vector<uint8_t> payload;
    bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
    CHECK(!pok, "Corrupted EOM: readPayload returns false when EOM is wrong");
}

// ---------------------------------------------------------------------------
// Test 4 — Resync recovery after a bad-EOM record
// ---------------------------------------------------------------------------
static void testResyncRecovery(const std::string& path)
{
    std::cout << "\n[Test 4] Resync recovery after corrupted EOM\n";

    constexpr uint32_t GOOD_TRACK_ID = 42u;
    constexpr uint64_t GOOD_TS       = 9'999'000'000ULL;

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        // First record: valid header, valid payload, but wrong EOM
        const uint32_t badId = 1u;
        writeRecordBadEOM(out,
                          static_cast<uint32_t>(cuas::LogRecordType::TrackDeleted),
                          1000ULL,
                          &badId,
                          sizeof(badId));

        // Second record: fully valid
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::TrackDeleted),
                    GOOD_TS,
                    &GOOD_TRACK_ID,
                    sizeof(GOOD_TRACK_ID));
    }

    std::ifstream in(path, std::ios::binary);

    // First read: header is good, payload EOM is bad
    cuas::LogRecordHeader hdr{};
    cuas::BinaryLogger::readHeader(in, hdr);
    std::vector<uint8_t> payload;
    bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
    CHECK(!pok, "Resync: first record readPayload fails on bad EOM");

    // Resync
    bool synced = cuas::BinaryLogger::resyncToNextRecord(in);
    CHECK(synced, "Resync: resyncToNextRecord finds next SOM after corruption");

    // Now the good record should be readable
    cuas::LogRecordHeader hdr2{};
    bool hok2 = cuas::BinaryLogger::readHeader(in, hdr2);
    CHECK(hok2,                              "Resync: readHeader OK after resync");
    CHECK(hdr2.magic == cuas::LOG_MAGIC,     "Resync: SOM magic correct after resync");
    CHECK(hdr2.timestamp == GOOD_TS,         "Resync: timestamp correct after resync");

    std::vector<uint8_t> payload2;
    bool pok2 = cuas::BinaryLogger::readPayload(in, hdr2.payloadSize, payload2);
    CHECK(pok2, "Resync: readPayload OK for good record after resync");

    if (payload2.size() >= 4u) {
        uint32_t tid = 0;
        std::memcpy(&tid, payload2.data(), 4);
        CHECK(tid == GOOD_TRACK_ID, "Resync: recovered record trackId == 42");
    }
}

// ---------------------------------------------------------------------------
// Test 5 — Zero-payload record (EOM immediately after header)
// ---------------------------------------------------------------------------
static void testZeroPayload(const std::string& path)
{
    std::cout << "\n[Test 5] Zero-payload record (EOM directly after header)\n";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        writeRecord(out,
                    static_cast<uint32_t>(cuas::LogRecordType::RunInfo),
                    0, nullptr, 0);
    }

    std::ifstream in(path, std::ios::binary);
    cuas::LogRecordHeader hdr{};
    bool hok = cuas::BinaryLogger::readHeader(in, hdr);
    CHECK(hok,                     "Zero-payload: readHeader OK");
    CHECK(hdr.payloadSize == 0u,   "Zero-payload: payloadSize == 0");

    std::vector<uint8_t> payload;
    bool pok = cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload);
    CHECK(pok,             "Zero-payload: readPayload OK (EOM still present and valid)");
    CHECK(payload.empty(), "Zero-payload: payload vector is empty");
}

// ---------------------------------------------------------------------------
// Test 6 — BinaryLogger write path (real API)
// ---------------------------------------------------------------------------
static void testBinaryLoggerAPI(const std::string& tmpDir)
{
    std::cout << "\n[Test 6] BinaryLogger write-path produces valid SOM/EOM records\n";

    std::filesystem::create_directories(tmpDir);
    cuas::BinaryLogger logger;
    bool opened = logger.open(tmpDir, "test", "Clusterer=DBSCAN");
    CHECK(opened, "BinaryLogger::open() succeeds");

    const std::string logPath = logger.getLogPath();
    CHECK(!logPath.empty(), "BinaryLogger::getLogPath() is non-empty");

    // Write a mix of record types via the high-level API
    const cuas::Timestamp ts = 5'000'000'000ULL;

    cuas::SPDetectionMessage dmsg{};
    dmsg.messageId     = 0x0001u;
    dmsg.dwellCount    = 1u;
    dmsg.timestamp     = ts;
    dmsg.numDetections = 2u;
    cuas::Detection d1{}, d2{};
    d1.range = 800.0;  d1.snr = 10.0;
    d2.range = 950.0;  d2.snr =  8.5;
    dmsg.detections = { d1, d2 };
    logger.logRawDetections(ts, dmsg);

    logger.logPreprocessed(ts + 100u, {d1, d2});

    cuas::StateVector sv{};
    sv[0] = 500.0; sv[3] = -100.0; sv[6] = 30.0;
    logger.logTrackInitiated(ts + 200u, 1u, sv);
    logger.logTrackUpdated(ts + 300u, 1u, sv, cuas::TrackStatusVal::Confirmed);
    logger.logAssociated(ts + 400u, 1u, 0u, 1.23);
    logger.logTrackDeleted(ts + 500u, 1u);

    logger.close();

    // Now read the file back and verify every record has a valid EOM
    std::ifstream in(logPath, std::ios::binary);
    CHECK(in.is_open(), "Log file created and readable");

    int recordCount = 0;
    int eomOkCount  = 0;
    cuas::LogRecordHeader hdr{};
    while (cuas::BinaryLogger::readHeader(in, hdr)) {
        ++recordCount;
        std::vector<uint8_t> payload;
        if (cuas::BinaryLogger::readPayload(in, hdr.payloadSize, payload))
            ++eomOkCount;
    }

    // 1 RunInfo + 1 RawDetection + 1 Preprocessed + 1 TrackInitiated
    // + 1 TrackUpdated + 1 Associated + 1 TrackDeleted = 7
    CHECK(recordCount == 7, "BinaryLogger wrote 7 records (RunInfo + 6 pipeline events)");
    CHECK(eomOkCount  == 7, "All 7 records have a valid EOM sentinel");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "====================================================\n";
    std::cout << "  Counter-UAS Log SOM/EOM Framing Tests\n";
    std::cout << "====================================================\n";

    const std::string tmpFile = "/tmp/cuas_test_som_eom.bin";
    const std::string tmpDir  = "/tmp/cuas_test_logger_api";

    testFramingConstants();
    testValidRoundtrip(tmpFile);
    testCorruptedEOM(tmpFile);
    testResyncRecovery(tmpFile);
    testZeroPayload(tmpFile);
    testBinaryLoggerAPI(tmpDir);

    std::cout << "\n====================================================\n";
    std::cout << "  Results: " << g_pass << " passed, "
              << g_fail << " failed\n";
    std::cout << "====================================================\n";

    // Clean up temp artefacts
    std::error_code ec;
    std::filesystem::remove(tmpFile, ec);
    std::filesystem::remove_all(tmpDir, ec);

    return (g_fail == 0) ? 0 : 1;
}
