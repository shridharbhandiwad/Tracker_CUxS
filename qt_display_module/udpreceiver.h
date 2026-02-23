#ifndef UDPRECEIVER_H
#define UDPRECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QVector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Raw detections  (MSG_ID_SP_DETECTION = 0x0001)
// ---------------------------------------------------------------------------
struct RawDetection {
    double range        = 0.0;  // metres
    double azimuth      = 0.0;  // radians
    double elevation    = 0.0;  // radians
    double strength     = 0.0;  // dBm
    double noise        = 0.0;  // dBm
    double snr          = 0.0;  // dB
    double rcs          = 0.0;  // dBsm
    double microDoppler = 0.0;  // Hz
};

struct RawDetectionFrame {
    uint32_t              dwellCount = 0;
    uint64_t              timestamp  = 0;
    QVector<RawDetection> detections;
};

// ---------------------------------------------------------------------------
// Track data  (MSG_ID_TRACK_TABLE = 0x0003 / MSG_ID_TRACK_UPDATE = 0x0002)
// ---------------------------------------------------------------------------
struct TrackData {
    uint32_t trackId        = 0;
    uint64_t timestamp      = 0;
    uint32_t status         = 0;   // 0=Tentative 1=Confirmed 2=Coasting 3=Deleted
    uint32_t classification = 0;   // 0=Unknown 1=DroneR 2=DroneF 3=Bird 4=Clutter
    double   range          = 0.0;
    double   azimuth        = 0.0; // radians
    double   elevation      = 0.0; // radians
    double   rangeRate      = 0.0;
    double   x = 0.0, y = 0.0, z = 0.0;
    double   vx = 0.0, vy = 0.0, vz = 0.0;
    double   trackQuality   = 0.0;
    uint32_t hitCount       = 0;
    uint32_t missCount      = 0;
    uint32_t age            = 0;
};

// ---------------------------------------------------------------------------
// Cluster data  (MSG_ID_CLUSTER_TABLE = 0x0010)
// ---------------------------------------------------------------------------
struct ClusterData {
    uint32_t clusterId     = 0;
    uint32_t numDetections = 0;
    double   range         = 0.0;
    double   azimuth       = 0.0;  // radians
    double   elevation     = 0.0;  // radians
    double   strength      = 0.0;
    double   snr           = 0.0;
    double   rcs           = 0.0;
    double   microDoppler  = 0.0;
    double   x = 0.0, y = 0.0, z = 0.0;
};

// ---------------------------------------------------------------------------
// Association entry  (MSG_ID_ASSOC_TABLE = 0x0011)
// ---------------------------------------------------------------------------
struct AssocEntry {
    uint32_t trackId   = 0;
    uint32_t clusterId = 0;    // 0xFFFFFFFF = unmatched track
    double   distance  = -1.0; // Mahalanobis distance; -1 = unmatched
    uint32_t matched   = 0;
};

// ---------------------------------------------------------------------------
// Predicted-state entry  (MSG_ID_PREDICTED_TABLE = 0x0012)
// ---------------------------------------------------------------------------
struct PredictedEntry {
    uint32_t trackId     = 0;
    uint32_t trackStatus = 0;
    double x  = 0, vx = 0, ax = 0;
    double y  = 0, vy = 0, ay = 0;
    double z  = 0, vz = 0, az = 0;
    double range = 0, azimuth = 0, elevation = 0;
    double covX = 0, covY = 0, covZ = 0;
    double modelProb[5] = {};
};

// ---------------------------------------------------------------------------
// UdpReceiver  – listens on a single port and dispatches all message types
// ---------------------------------------------------------------------------
class UdpReceiver : public QObject {
    Q_OBJECT
public:
    explicit UdpReceiver(quint16 port, QObject *parent = nullptr);
    bool    bind();
    quint16 port() const { return port_; }

signals:
    void tracksReceived      (const QVector<TrackData>      &tracks);
    void rawDetectionsReceived(const RawDetectionFrame       &frame);
    void clustersReceived    (const QVector<ClusterData>    &clusters,
                              quint64 timestamp, quint32 dwellCount);
    void assocReceived       (const QVector<AssocEntry>     &entries,
                              quint64 timestamp);
    void predictedReceived   (const QVector<PredictedEntry> &entries,
                              quint64 timestamp);

private slots:
    void onReadyRead();

private:
    void dispatchMessage    (const QByteArray &data);
    void decodeDetections   (const char *p, int len);
    void decodeTrackTable   (const char *p, int len);
    void decodeSingleTrack  (const char *p, int len);
    void decodeClusterTable (const char *p, int len);
    void decodeAssocTable   (const char *p, int len);
    void decodePredictedTable(const char *p, int len);

    QUdpSocket *socket_;
    quint16     port_;
};

#endif // UDPRECEIVER_H
