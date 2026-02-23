#include "udpreceiver.h"

#include <QNetworkDatagram>
#include <cstring>

// ---------------------------------------------------------------------------
// Message IDs (must match cuas::constants.h in the tracker)
// ---------------------------------------------------------------------------
static constexpr uint32_t MSG_SP_DETECTION    = 0x0001;
static constexpr uint32_t MSG_TRACK_UPDATE    = 0x0002;
static constexpr uint32_t MSG_TRACK_TABLE     = 0x0003;
static constexpr uint32_t MSG_CLUSTER_TABLE   = 0x0010;
static constexpr uint32_t MSG_ASSOC_TABLE     = 0x0011;
static constexpr uint32_t MSG_PREDICTED_TABLE = 0x0012;

// ---------------------------------------------------------------------------
// Wire-format sizes
// ---------------------------------------------------------------------------
// TrackUpdateMessage (natural alignment, 64-bit MSVC):
//   [0]   messageId u32     [4]   trackId u32      [8]  timestamp u64
//  [16]   status u32       [20]   class u32        [24] range d8
//  [32]   azimuth d8       [40]   elevation d8     [48] rangeRate d8
//  [56]   x d8             [64]   y d8             [72] z d8
//  [80]   vx d8            [88]   vy d8            [96] vz d8
// [104]   trackQuality d8  [112]  hitCount u32    [116] missCount u32
// [120]   age u32          [124]  (4-byte padding)  → total 128 bytes
static constexpr int TRACK_SIZE   = 128;

// Detection (no padding): 8 doubles = 64 bytes
static constexpr int DET_SIZE     = 64;

// ClusterWire (#pragma pack(1)): 2×u32 + 10×d8 = 88 bytes
static constexpr int CLUSTER_SIZE = 88;

// AssocEntryWire (#pragma pack(1)): 2×u32 + d8 + 2×u32 = 24 bytes
static constexpr int ASSOC_SIZE   = 24;

// PredictedEntryWire (#pragma pack(1)): 2×u32 + 20×d8 = 168 bytes
static constexpr int PRED_SIZE    = 168;

// ---------------------------------------------------------------------------
// Low-level readers (safe on unaligned data)
// ---------------------------------------------------------------------------
static inline uint32_t rd32(const char *s) { uint32_t v; std::memcpy(&v,s,4); return v; }
static inline uint64_t rd64(const char *s) { uint64_t v; std::memcpy(&v,s,8); return v; }
static inline double   rdD (const char *s) { double   v; std::memcpy(&v,s,8); return v; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
UdpReceiver::UdpReceiver(quint16 port, QObject *parent)
    : QObject(parent)
    , socket_(new QUdpSocket(this))
    , port_(port)
{
    connect(socket_, &QUdpSocket::readyRead, this, &UdpReceiver::onReadyRead);
}

bool UdpReceiver::bind()
{
    return socket_->bind(QHostAddress::Any, port_);
}

// ---------------------------------------------------------------------------
// Incoming datagram dispatch
// ---------------------------------------------------------------------------
void UdpReceiver::onReadyRead()
{
    while (socket_->hasPendingDatagrams()) {
        QByteArray data = socket_->receiveDatagram().data();
        if (data.size() >= 4)
            dispatchMessage(data);
    }
}

void UdpReceiver::dispatchMessage(const QByteArray &data)
{
    uint32_t msgId = rd32(data.constData());
    const char *p  = data.constData();
    int         n  = data.size();

    switch (msgId) {
    case MSG_SP_DETECTION:    decodeDetections(p, n);     break;
    case MSG_TRACK_UPDATE:    decodeSingleTrack(p, n);    break;
    case MSG_TRACK_TABLE:     decodeTrackTable(p, n);     break;
    case MSG_CLUSTER_TABLE:   decodeClusterTable(p, n);   break;
    case MSG_ASSOC_TABLE:     decodeAssocTable(p, n);     break;
    case MSG_PREDICTED_TABLE: decodePredictedTable(p, n); break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// MSG_SP_DETECTION (0x0001)
// Header: msgId(4) dwellCount(4) timestamp(8) numDets(4)  = 20 bytes
// Detection: range az el strength noise snr rcs microDoppler  (8 doubles, 64 bytes)
// ---------------------------------------------------------------------------
void UdpReceiver::decodeDetections(const char *p, int len)
{
    if (len < 20) return;

    RawDetectionFrame frame;
    frame.dwellCount = rd32(p + 4);
    frame.timestamp  = rd64(p + 8);
    uint32_t n       = rd32(p + 16);

    if (len < 20 + static_cast<int>(n) * DET_SIZE) return;

    frame.detections.resize(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i) {
        const char  *dp = p + 20 + i * DET_SIZE;
        RawDetection &d = frame.detections[static_cast<int>(i)];
        d.range        = rdD(dp +  0);
        d.azimuth      = rdD(dp +  8);
        d.elevation    = rdD(dp + 16);
        d.strength     = rdD(dp + 24);
        d.noise        = rdD(dp + 32);
        d.snr          = rdD(dp + 40);
        d.rcs          = rdD(dp + 48);
        d.microDoppler = rdD(dp + 56);
    }
    emit rawDetectionsReceived(frame);
}

// ---------------------------------------------------------------------------
// Helper: read one TrackUpdateMessage entry (128 bytes) → TrackData
// The first 4 bytes of the entry are the per-message messageId (skipped).
// ---------------------------------------------------------------------------
static void readTrackEntry(const char *ep, TrackData &t)
{
    t.trackId        = rd32(ep +   4);
    t.timestamp      = rd64(ep +   8);
    t.status         = rd32(ep +  16);
    t.classification = rd32(ep +  20);
    t.range          = rdD (ep +  24);
    t.azimuth        = rdD (ep +  32);
    t.elevation      = rdD (ep +  40);
    t.rangeRate      = rdD (ep +  48);
    t.x              = rdD (ep +  56);
    t.y              = rdD (ep +  64);
    t.z              = rdD (ep +  72);
    t.vx             = rdD (ep +  80);
    t.vy             = rdD (ep +  88);
    t.vz             = rdD (ep +  96);
    t.trackQuality   = rdD (ep + 104);
    t.hitCount       = rd32(ep + 112);
    t.missCount      = rd32(ep + 116);
    t.age            = rd32(ep + 120);
}

// ---------------------------------------------------------------------------
// MSG_TRACK_TABLE (0x0003)
// Header: msgId(4) timestamp(8) numTracks(4) = 16 bytes
// Entry:  TrackUpdateMessage = 128 bytes each
// ---------------------------------------------------------------------------
void UdpReceiver::decodeTrackTable(const char *p, int len)
{
    if (len < 16) return;
    uint32_t n = rd32(p + 12);

    if (len < 16 + static_cast<int>(n) * TRACK_SIZE) return;

    QVector<TrackData> tracks(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i)
        readTrackEntry(p + 16 + i * TRACK_SIZE, tracks[static_cast<int>(i)]);

    emit tracksReceived(tracks);
}

// ---------------------------------------------------------------------------
// MSG_TRACK_UPDATE (0x0002)  – bare TrackUpdateMessage, 128 bytes
// ---------------------------------------------------------------------------
void UdpReceiver::decodeSingleTrack(const char *p, int len)
{
    if (len < TRACK_SIZE) return;
    TrackData t;
    readTrackEntry(p, t);
    emit tracksReceived({t});
}

// ---------------------------------------------------------------------------
// MSG_CLUSTER_TABLE (0x0010)
// Header: msgId(4) timestamp(8) dwellCount(4) numItems(4) = 20 bytes
// ClusterWire (packed): clusterId u32, numDets u32, range az el str snr rcs md x y z (10×d8) = 88 bytes
// ---------------------------------------------------------------------------
void UdpReceiver::decodeClusterTable(const char *p, int len)
{
    if (len < 20) return;
    quint64  ts         = rd64(p +  4);
    quint32  dwellCount = rd32(p + 12);
    uint32_t n          = rd32(p + 16);

    if (len < 20 + static_cast<int>(n) * CLUSTER_SIZE) return;

    QVector<ClusterData> clusters(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i) {
        const char *cp = p + 20 + i * CLUSTER_SIZE;
        ClusterData &c = clusters[static_cast<int>(i)];
        c.clusterId     = rd32(cp +  0);
        c.numDetections = rd32(cp +  4);
        c.range         = rdD (cp +  8);
        c.azimuth       = rdD (cp + 16);
        c.elevation     = rdD (cp + 24);
        c.strength      = rdD (cp + 32);
        c.snr           = rdD (cp + 40);
        c.rcs           = rdD (cp + 48);
        c.microDoppler  = rdD (cp + 56);
        c.x             = rdD (cp + 64);
        c.y             = rdD (cp + 72);
        c.z             = rdD (cp + 80);
    }
    emit clustersReceived(clusters, ts, dwellCount);
}

// ---------------------------------------------------------------------------
// MSG_ASSOC_TABLE (0x0011)
// Header: msgId(4) timestamp(8) numItems(4) = 16 bytes
// AssocEntryWire (packed): trackId u32, clusterId u32, distance d8, matched u32, pad u32 = 24 bytes
// ---------------------------------------------------------------------------
void UdpReceiver::decodeAssocTable(const char *p, int len)
{
    if (len < 16) return;
    quint64  ts = rd64(p + 4);
    uint32_t n  = rd32(p + 12);

    if (len < 16 + static_cast<int>(n) * ASSOC_SIZE) return;

    QVector<AssocEntry> entries(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i) {
        const char *ep = p + 16 + i * ASSOC_SIZE;
        AssocEntry  &e = entries[static_cast<int>(i)];
        e.trackId   = rd32(ep +  0);
        e.clusterId = rd32(ep +  4);
        e.distance  = rdD (ep +  8);
        e.matched   = rd32(ep + 16);
        // ep+20 is the wire pad field – skip
    }
    emit assocReceived(entries, ts);
}

// ---------------------------------------------------------------------------
// MSG_PREDICTED_TABLE (0x0012)
// Header: msgId(4) timestamp(8) numItems(4) = 16 bytes
// PredictedEntryWire (packed):
//   trackId u32, trackStatus u32,
//   x vx ax y vy ay z vz az (9×d8),
//   range az el (3×d8), covX covY covZ (3×d8), modelProb[5] (5×d8)
//   total: 8 + 20×8 = 168 bytes
// ---------------------------------------------------------------------------
void UdpReceiver::decodePredictedTable(const char *p, int len)
{
    if (len < 16) return;
    quint64  ts = rd64(p + 4);
    uint32_t n  = rd32(p + 12);

    if (len < 16 + static_cast<int>(n) * PRED_SIZE) return;

    QVector<PredictedEntry> entries(static_cast<int>(n));
    for (uint32_t i = 0; i < n; ++i) {
        const char     *ep = p + 16 + i * PRED_SIZE;
        PredictedEntry  &e = entries[static_cast<int>(i)];
        e.trackId     = rd32(ep +   0);
        e.trackStatus = rd32(ep +   4);
        e.x  = rdD(ep +   8); e.vx = rdD(ep +  16); e.ax = rdD(ep +  24);
        e.y  = rdD(ep +  32); e.vy = rdD(ep +  40); e.ay = rdD(ep +  48);
        e.z  = rdD(ep +  56); e.vz = rdD(ep +  64); e.az = rdD(ep +  72);
        e.range     = rdD(ep +  80);
        e.azimuth   = rdD(ep +  88);
        e.elevation = rdD(ep +  96);
        e.covX      = rdD(ep + 104);
        e.covY      = rdD(ep + 112);
        e.covZ      = rdD(ep + 120);
        for (int m = 0; m < 5; ++m)
            e.modelProb[m] = rdD(ep + 128 + m * 8);
    }
    emit predictedReceived(entries, ts);
}
