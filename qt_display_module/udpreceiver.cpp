#include "udpreceiver.h"
#include <QNetworkDatagram>
#include <cstring>

static constexpr uint32_t MSG_ID_TRACK_TABLE  = 0x0003;

/*
 * Binary layout of a single TrackUpdateMessage as emitted by the tracker
 * (plain memcpy of the C++ struct, compiled for the same platform).
 *
 * Offsets (64-bit Windows / MSVC, natural alignment):
 *   0  messageId      uint32
 *   4  trackId        uint32
 *   8  timestamp      uint64
 *  16  status         uint32
 *  20  classification uint32
 *  24  range          double
 *  32  azimuth        double
 *  40  elevation      double
 *  48  rangeRate      double
 *  56  x              double
 *  64  y              double
 *  72  z              double
 *  80  vx             double
 *  88  vy             double
 *  96  vz             double
 * 104  trackQuality   double
 * 112  hitCount       uint32
 * 116  missCount      uint32
 * 120  age            uint32
 * 124  (padding to 128 for 8-byte struct alignment)
 */
static constexpr int TRACK_MSG_SIZE = 128;

static void readTrack(const char *p, TrackData &t)
{
    auto rd32 = [](const char *src) -> uint32_t {
        uint32_t v; std::memcpy(&v, src, 4); return v;
    };
    auto rd64 = [](const char *src) -> uint64_t {
        uint64_t v; std::memcpy(&v, src, 8); return v;
    };
    auto rdD = [](const char *src) -> double {
        double v; std::memcpy(&v, src, 8); return v;
    };

    // skip messageId at offset 0
    t.trackId        = rd32(p +  4);
    t.timestamp      = rd64(p +  8);
    t.status         = rd32(p + 16);
    t.classification = rd32(p + 20);
    t.range          = rdD (p + 24);
    t.azimuth        = rdD (p + 32);
    t.elevation      = rdD (p + 40);
    t.rangeRate      = rdD (p + 48);
    t.x              = rdD (p + 56);
    t.y              = rdD (p + 64);
    t.z              = rdD (p + 72);
    t.vx             = rdD (p + 80);
    t.vy             = rdD (p + 88);
    t.vz             = rdD (p + 96);
    t.trackQuality   = rdD (p +104);
    t.hitCount       = rd32(p +112);
    t.missCount      = rd32(p +116);
    t.age            = rd32(p +120);
}

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

void UdpReceiver::onReadyRead()
{
    while (socket_->hasPendingDatagrams()) {
        QByteArray data = socket_->receiveDatagram().data();
        if (data.isEmpty())
            continue;

        QVector<TrackData> tracks;
        if (decodeTrackTable(data, tracks)) {
            emit tracksReceived(tracks);
        } else {
            TrackData single;
            if (decodeSingleTrack(data, single))
                emit tracksReceived({single});
        }
    }
}

bool UdpReceiver::decodeSingleTrack(const QByteArray &data, TrackData &out)
{
    if (data.size() < TRACK_MSG_SIZE)
        return false;
    readTrack(data.constData(), out);
    return true;
}

bool UdpReceiver::decodeTrackTable(const QByteArray &data, QVector<TrackData> &out)
{
    if (data.size() < 16)
        return false;

    const char *p = data.constData();
    uint32_t msgId;
    std::memcpy(&msgId, p, 4);
    if (msgId != MSG_ID_TRACK_TABLE)
        return false;

    // skip timestamp (8 bytes at offset 4)
    uint32_t numTracks;
    std::memcpy(&numTracks, p + 12, 4);

    int expected = 16 + static_cast<int>(numTracks) * TRACK_MSG_SIZE;
    if (data.size() < expected)
        return false;

    out.resize(static_cast<int>(numTracks));
    for (uint32_t i = 0; i < numTracks; ++i)
        readTrack(p + 16 + i * TRACK_MSG_SIZE, out[static_cast<int>(i)]);

    return true;
}
