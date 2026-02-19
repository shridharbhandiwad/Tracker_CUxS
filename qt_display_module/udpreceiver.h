#ifndef UDPRECEIVER_H
#define UDPRECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QVector>
#include <cstdint>

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

class UdpReceiver : public QObject {
    Q_OBJECT
public:
    explicit UdpReceiver(quint16 port, QObject *parent = nullptr);
    bool bind();
    quint16 port() const { return port_; }

signals:
    void tracksReceived(const QVector<TrackData> &tracks);

private slots:
    void onReadyRead();

private:
    bool decodeSingleTrack(const QByteArray &data, TrackData &out);
    bool decodeTrackTable(const QByteArray &data, QVector<TrackData> &out);

    QUdpSocket *socket_;
    quint16     port_;
};

#endif // UDPRECEIVER_H
