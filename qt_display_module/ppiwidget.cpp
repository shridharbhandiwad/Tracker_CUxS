#include "ppiwidget.h"

#include <QPainter>
#include <QPen>
#include <QWheelEvent>
#include <QMouseEvent>
#include <cmath>

static constexpr double PI = 3.14159265358979323846;
static constexpr double RAD2DEG = 180.0 / PI;

PPIWidget::PPIWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void PPIWidget::setDetections(const RawDetectionFrame &frame)
{
    dets_ = frame;
    // Adjust maxRange from data
    for (const auto &d : frame.detections)
        if (d.range > maxRange_) maxRange_ = std::ceil(d.range / 1000.0) * 1000.0;
    update();
}

void PPIWidget::setTracks(const QVector<TrackData> &tracks)
{
    tracks_ = tracks;
    for (const auto &t : tracks)
        if (t.range > maxRange_) maxRange_ = std::ceil(t.range / 1000.0) * 1000.0;
    update();
}

// World (metres) → screen pixels.
// Radar at widget centre; X→East (right), Y→North (up, so flipped on screen).
QPointF PPIWidget::toScreen(double x, double y) const
{
    double cx = width()  / 2.0 + panOffset_.x();
    double cy = height() / 2.0 + panOffset_.y();
    double ppm = (std::min(width(), height()) / 2.0) * scale_ / maxRange_;
    return QPointF(cx + x * ppm, cy - y * ppm);
}

void PPIWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(15, 20, 15));

    double cx = width()  / 2.0 + panOffset_.x();
    double cy = height() / 2.0 + panOffset_.y();
    double R  = (std::min(width(), height()) / 2.0) * scale_;

    // Range rings (4 × 25%)
    QPen ringPen(QColor(40, 120, 40), 1, Qt::DashLine);
    p.setPen(ringPen);
    p.setBrush(Qt::NoBrush);
    for (int ring = 1; ring <= 4; ++ring) {
        double r = R * ring / 4.0;
        p.drawEllipse(QPointF(cx, cy), r, r);
        QString lbl = QString("%1 km").arg(maxRange_ * ring / 4.0 / 1000.0, 0, 'f', 1);
        p.setPen(QColor(80, 160, 80));
        p.drawText(QPointF(cx + r + 2, cy - 2), lbl);
        p.setPen(ringPen);
    }

    // Azimuth spokes every 30°
    QPen spokePen(QColor(30, 80, 30), 1, Qt::DotLine);
    p.setPen(spokePen);
    for (int deg = 0; deg < 360; deg += 30) {
        double rad = deg * PI / 180.0;
        // azimuth 0=East, positive CCW
        p.drawLine(QPointF(cx, cy),
                   QPointF(cx + R * std::cos(rad), cy - R * std::sin(rad)));
        // Label
        QString lbl = QString("%1°").arg(deg);
        p.setPen(QColor(60, 100, 60));
        p.drawText(QPointF(cx + (R + 8) * std::cos(rad) - 8,
                           cy - (R + 8) * std::sin(rad) + 4), lbl);
        p.setPen(spokePen);
    }

    // North arrow label
    p.setPen(QColor(200, 255, 200));
    p.setFont(QFont("Arial", 9, QFont::Bold));
    p.drawText(QPointF(cx - 4, cy - R - 8), "N");

    // Raw detections — small green dots
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 220, 80, 180));
    for (const auto &d : dets_.detections) {
        // Use horizontal Cartesian (top-down view, ignore elevation for plot)
        double wx = d.range * std::cos(d.azimuth);
        double wy = d.range * std::sin(d.azimuth);
        QPointF sp = toScreen(wx, wy);
        p.drawEllipse(sp, 3.0, 3.0);
    }

    // Tracks
    QFont idFont("Arial", 7);
    p.setFont(idFont);
    for (const auto &t : tracks_) {
        QPointF sp = toScreen(t.x, t.y);
        bool confirmed = (t.status == 1);
        bool coasting  = (t.status == 2);

        QColor col = confirmed ? QColor(255, 80, 80)
                   : coasting  ? QColor(255, 180, 0)
                               : QColor(100, 160, 255);

        // Draw a cross/diamond symbol
        p.setPen(QPen(col, 2));
        p.setBrush(Qt::NoBrush);
        double sz = confirmed ? 7.0 : 5.0;
        if (confirmed) {
            // Triangle
            QPolygonF tri;
            tri << QPointF(sp.x(), sp.y() - sz)
                << QPointF(sp.x() + sz * 0.866, sp.y() + sz * 0.5)
                << QPointF(sp.x() - sz * 0.866, sp.y() + sz * 0.5);
            p.drawPolygon(tri);
        } else {
            p.drawLine(QPointF(sp.x() - sz, sp.y()), QPointF(sp.x() + sz, sp.y()));
            p.drawLine(QPointF(sp.x(), sp.y() - sz), QPointF(sp.x(), sp.y() + sz));
        }

        // Velocity vector (3 s look-ahead)
        if (confirmed) {
            double vscale = 3.0;
            QPointF vEnd = toScreen(t.x + t.vx * vscale, t.y + t.vy * vscale);
            p.setPen(QPen(col, 1, Qt::DashLine));
            p.drawLine(sp, vEnd);
        }

        // ID label
        p.setPen(col);
        p.drawText(QPointF(sp.x() + sz + 2, sp.y() + 4),
                   QString("#%1").arg(t.trackId));
    }

    // Radar center dot
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 100));
    p.drawEllipse(QPointF(cx, cy), 5.0, 5.0);
    p.setPen(QColor(255, 255, 100));
    p.drawText(QPointF(cx + 6, cy + 4), "RAD");

    // Legend
    p.setFont(QFont("Arial", 8));
    int ly = height() - 60;
    p.setPen(QColor(0, 220, 80));  p.drawText(10, ly,      "● Raw Det");
    p.setPen(QColor(255, 80, 80)); p.drawText(10, ly + 14, "▲ Confirmed");
    p.setPen(QColor(100, 160, 255)); p.drawText(10, ly + 28, "+ Tentative");
    p.setPen(QColor(255, 180, 0)); p.drawText(10, ly + 42, "+ Coasting");

    // Stats overlay
    p.setPen(QColor(180, 255, 180));
    p.setFont(QFont("Consolas", 8));
    p.drawText(10, 20, QString("Max range: %1 km  |  Zoom: %2x  |  Dets: %3  |  Tracks: %4")
               .arg(maxRange_ / 1000.0, 0, 'f', 1)
               .arg(scale_, 0, 'f', 2)
               .arg(dets_.detections.size())
               .arg(tracks_.size()));
}

void PPIWidget::wheelEvent(QWheelEvent *event)
{
    double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale_ = qBound(0.1, scale_ * factor, 20.0);
    update();
}

void PPIWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        panning_   = true;
        lastMouse_ = event->pos();
    }
}

void PPIWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_) {
        QPointF delta = event->pos() - lastMouse_;
        panOffset_ += delta;
        lastMouse_ = event->pos();
        update();
    }
}

void PPIWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        panning_ = false;
}

void PPIWidget::resizeEvent(QResizeEvent *)
{
    update();
}
