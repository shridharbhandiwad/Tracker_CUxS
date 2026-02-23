#include "scopewidget.h"

#include <QPainter>
#include <QPen>
#include <cmath>

static constexpr double PI = 3.14159265358979323846;
static constexpr double RAD2DEG = 180.0 / PI;

ScopeWidget::ScopeWidget(Mode mode, QWidget *parent)
    : QWidget(parent), mode_(mode)
{
    setMinimumSize(300, 200);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void ScopeWidget::setDetections(const RawDetectionFrame &frame)
{
    dets_ = frame;
    if (mode_ == BScope) {
        for (const auto &d : frame.detections)
            if (d.range > maxRange_) maxRange_ = std::ceil(d.range / 1000.0) * 1000.0;
    }
    update();
}

void ScopeWidget::setTracks(const QVector<TrackData> &tracks)
{
    tracks_ = tracks;
    if (mode_ == BScope) {
        for (const auto &t : tracks)
            if (t.range > maxRange_) maxRange_ = std::ceil(t.range / 1000.0) * 1000.0;
    }
    update();
}

// Map data coords to plot rect pixels.
// B-scope: hVal=azimuth(deg), vVal=range(m)   → top=maxRange, bottom=0
// C-scope: hVal=azimuth(deg), vVal=elev(deg)  → top=90°, bottom=-10°
QPointF ScopeWidget::dataToPlot(double hVal, double vVal, const QRectF &plot) const
{
    // Horizontal: azimuth -180 to +180
    double hNorm = (hVal + 180.0) / 360.0;

    double vNorm;
    if (mode_ == BScope) {
        vNorm = vVal / maxRange_;               // 0=bottom, 1=top
    } else {
        const double EL_MIN = -10.0, EL_MAX = 90.0;
        vNorm = (vVal - EL_MIN) / (EL_MAX - EL_MIN);
    }
    vNorm = qBound(0.0, vNorm, 1.0);
    hNorm = qBound(0.0, hNorm, 1.0);

    double px = plot.left() + hNorm * plot.width();
    double py = plot.bottom() - vNorm * plot.height(); // screen Y inverted
    return QPointF(px, py);
}

void ScopeWidget::drawGrid(QPainter &p, const QRectF &plot) const
{
    QPen gridPen(QColor(50, 80, 50), 1, Qt::DotLine);
    p.setPen(gridPen);

    // Vertical grid lines: azimuth every 30°
    for (int az = -180; az <= 180; az += 30) {
        QPointF bot = dataToPlot(az, 0.0, plot);
        QPointF top = (mode_ == BScope)
                          ? dataToPlot(az, maxRange_, plot)
                          : dataToPlot(az, 90.0, plot);
        p.drawLine(bot, top);
    }

    // Horizontal grid lines
    if (mode_ == BScope) {
        for (int r = 0; r <= 4; ++r) {
            double rng = maxRange_ * r / 4.0;
            QPointF lft = dataToPlot(-180.0, rng, plot);
            QPointF rgt = dataToPlot( 180.0, rng, plot);
            p.drawLine(lft, rgt);
        }
    } else {
        for (int el = -10; el <= 90; el += 10) {
            QPointF lft = dataToPlot(-180.0, el, plot);
            QPointF rgt = dataToPlot( 180.0, el, plot);
            p.drawLine(lft, rgt);
        }
    }
}

void ScopeWidget::drawAxesLabels(QPainter &p, const QRectF &plot) const
{
    p.setPen(QColor(140, 200, 140));
    p.setFont(QFont("Consolas", 8));

    // X-axis labels (azimuth)
    for (int az = -180; az <= 180; az += 30) {
        QPointF pt = dataToPlot(az, 0.0, plot);
        p.drawText(QPointF(pt.x() - 12, plot.bottom() + 14), QString("%1°").arg(az));
    }

    // Y-axis labels
    if (mode_ == BScope) {
        for (int r = 0; r <= 4; ++r) {
            double rng = maxRange_ * r / 4.0;
            QPointF pt = dataToPlot(-180.0, rng, plot);
            p.drawText(QPointF(2, pt.y() + 4),
                       QString("%1k").arg(rng / 1000.0, 0, 'f', 1));
        }
    } else {
        for (int el = -10; el <= 90; el += 10) {
            QPointF pt = dataToPlot(-180.0, el, plot);
            p.drawText(QPointF(2, pt.y() + 4), QString("%1°").arg(el));
        }
    }

    // Axis titles
    p.setFont(QFont("Arial", 8));
    p.drawText(QPointF(plot.center().x() - 30, plot.bottom() + 26), "Azimuth (deg)");
    p.save();
    p.translate(10, plot.center().y());
    p.rotate(-90);
    p.drawText(QPointF(-30, 0), (mode_ == BScope) ? "Range (m)" : "Elevation (deg)");
    p.restore();
}

void ScopeWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(15, 20, 15));

    // Plot area with margins
    QRectF plot(48, 10, width() - 58, height() - 34);
    if (plot.width() < 10 || plot.height() < 10) return;

    // Border
    p.setPen(QColor(80, 140, 80));
    p.setBrush(QColor(10, 15, 10));
    p.drawRect(plot);

    // Grid & labels
    drawGrid(p, plot);
    drawAxesLabels(p, plot);

    // Plot detections
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 220, 80, 200));
    for (const auto &d : dets_.detections) {
        double hv = d.azimuth * RAD2DEG;
        double vv = (mode_ == BScope) ? d.range : d.elevation * RAD2DEG;
        QPointF sp = dataToPlot(hv, vv, plot);
        p.drawEllipse(sp, 3.0, 3.0);
    }

    // Plot tracks
    for (const auto &t : tracks_) {
        double hv = t.azimuth * RAD2DEG;
        double vv = (mode_ == BScope) ? t.range : t.elevation * RAD2DEG;
        QPointF sp = dataToPlot(hv, vv, plot);

        bool confirmed = (t.status == 1);
        QColor col = confirmed ? QColor(255, 80, 80) : QColor(100, 160, 255);

        p.setPen(QPen(col, 2));
        p.setBrush(Qt::NoBrush);
        double sz = confirmed ? 7.0 : 5.0;
        p.drawLine(QPointF(sp.x() - sz, sp.y()), QPointF(sp.x() + sz, sp.y()));
        p.drawLine(QPointF(sp.x(), sp.y() - sz), QPointF(sp.x(), sp.y() + sz));

        p.setFont(QFont("Arial", 7));
        p.setPen(col);
        p.drawText(QPointF(sp.x() + sz + 1, sp.y() + 4),
                   QString("#%1").arg(t.trackId));
    }

    // Title
    p.setPen(QColor(200, 255, 200));
    p.setFont(QFont("Arial", 8));
    QString title = (mode_ == BScope)
        ? QString("B-Scope (Range vs Az) — %1 dets  %2 tracks")
              .arg(dets_.detections.size()).arg(tracks_.size())
        : QString("C-Scope (El vs Az) — %1 dets  %2 tracks")
              .arg(dets_.detections.size()).arg(tracks_.size());
    p.drawText(QPointF(50, 22), title);
}
