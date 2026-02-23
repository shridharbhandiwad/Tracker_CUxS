#include "timeserieswidget.h"

#include <QPainter>
#include <QHBoxLayout>
#include <QLabel>
#include <cmath>

static constexpr double PI = 3.14159265358979323846;
static constexpr double RAD2DEG = 180.0 / PI;

TimeSeriesWidget::TimeSeriesWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    // Controls widget (embedded as a row above the chart)
    controls_ = new QWidget(this);
    QHBoxLayout *clo = new QHBoxLayout(controls_);
    clo->setContentsMargins(4, 2, 4, 2);
    clo->addWidget(new QLabel(tr("Track:"), controls_));
    trackCombo_ = new QComboBox(controls_);
    trackCombo_->setMinimumWidth(80);
    trackCombo_->addItem(tr("(none)"), 0u);
    clo->addWidget(trackCombo_);
    clo->addStretch();

    connect(trackCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimeSeriesWidget::onTrackChanged);
}

void TimeSeriesWidget::updateTracks(const QVector<TrackData> &tracks)
{
    // Update history for each received track
    for (const auto &t : tracks) {
        auto &h = history_[t.trackId];
        double spd = std::sqrt(t.vx*t.vx + t.vy*t.vy + t.vz*t.vz);
        h.range.push_back(t.range);
        h.azimuth.push_back(t.azimuth * RAD2DEG);
        h.elevation.push_back(t.elevation * RAD2DEG);
        h.rangeRate.push_back(t.rangeRate);
        h.quality.push_back(t.trackQuality);
        h.speed.push_back(spd);
        h.timestamps.push_back(t.timestamp);
        while (static_cast<int>(h.range.size())     > MAX_HISTORY) h.range.pop_front();
        while (static_cast<int>(h.azimuth.size())   > MAX_HISTORY) h.azimuth.pop_front();
        while (static_cast<int>(h.elevation.size()) > MAX_HISTORY) h.elevation.pop_front();
        while (static_cast<int>(h.rangeRate.size()) > MAX_HISTORY) h.rangeRate.pop_front();
        while (static_cast<int>(h.quality.size())   > MAX_HISTORY) h.quality.pop_front();
        while (static_cast<int>(h.speed.size())     > MAX_HISTORY) h.speed.pop_front();
        while (static_cast<int>(h.timestamps.size())> MAX_HISTORY) h.timestamps.pop_front();
    }

    // Sync combo box: add new tracks
    QSet<uint32_t> activeIds;
    for (const auto &t : tracks) activeIds.insert(t.trackId);

    for (const auto &t : tracks) {
        bool found = false;
        for (int i = 0; i < trackCombo_->count(); ++i) {
            if (trackCombo_->itemData(i).toUInt() == t.trackId) { found = true; break; }
        }
        if (!found) {
            trackCombo_->addItem(QString("Track #%1").arg(t.trackId), t.trackId);
        }
    }

    // If no track selected and there are some, auto-select first confirmed
    if (selectedTrackId_ == 0 && !history_.isEmpty()) {
        for (const auto &t : tracks) {
            if (t.status == 1) {
                for (int i = 0; i < trackCombo_->count(); ++i) {
                    if (trackCombo_->itemData(i).toUInt() == t.trackId) {
                        trackCombo_->setCurrentIndex(i);
                        break;
                    }
                }
                break;
            }
        }
    }

    update();
}

void TimeSeriesWidget::onTrackChanged(int index)
{
    selectedTrackId_ = trackCombo_->itemData(index).toUInt();
    update();
}

// Draw one channel in a sub-rect with label and autoscaled Y
void TimeSeriesWidget::drawChannel(QPainter &p, const QRectF &rect,
                                    const std::deque<double> &data,
                                    const QString &label,
                                    const QColor &color) const
{
    // Background
    p.fillRect(rect, QColor(10, 15, 10));
    p.setPen(QColor(50, 80, 50));
    p.drawRect(rect);

    if (data.empty()) {
        p.setPen(QColor(100, 100, 100));
        p.setFont(QFont("Consolas", 8));
        p.drawText(rect, Qt::AlignCenter, label + "\n(no data)");
        return;
    }

    // Find min/max
    double mn = data[0], mx = data[0];
    for (const auto &v : data) { mn = std::min(mn, v); mx = std::max(mx, v); }
    if (mx - mn < 1e-6) mx = mn + 1.0;

    // Plot area with a small left margin for labels
    QRectF plot(rect.left() + 42, rect.top() + 4, rect.width() - 46, rect.height() - 8);
    if (plot.width() < 2 || plot.height() < 2) return;

    // Y-axis labels
    p.setPen(QColor(140, 180, 140));
    p.setFont(QFont("Consolas", 7));
    for (int i = 0; i <= 4; ++i) {
        double val = mn + (mx - mn) * i / 4.0;
        double py  = plot.bottom() - (val - mn) / (mx - mn) * plot.height();
        p.drawText(QPointF(rect.left() + 1, py + 3), QString::number(val, 'f', 1));
        p.setPen(QColor(30, 60, 30));
        p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
        p.setPen(QColor(140, 180, 140));
    }

    // Channel label
    p.setPen(color);
    p.setFont(QFont("Arial", 8, QFont::Bold));
    p.drawText(QPointF(rect.left() + 1, rect.top() + 12), label);

    // Data line
    int n = static_cast<int>(data.size());
    QPolygonF poly;
    poly.reserve(n);
    for (int i = 0; i < n; ++i) {
        double px = plot.left() + (double)i / (MAX_HISTORY - 1) * plot.width();
        double py = plot.bottom() - (data[i] - mn) / (mx - mn) * plot.height();
        poly << QPointF(px, py);
    }
    p.setPen(QPen(color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);

    // Current value
    p.setPen(Qt::white);
    p.setFont(QFont("Consolas", 8));
    p.drawText(QPointF(plot.right() - 55, rect.top() + 12),
               QString("= %1").arg(data.back(), 0, 'f', 2));
}

void TimeSeriesWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), QColor(10, 12, 10));

    // Reserve space for controls at top (30 px)
    int ctrlH = controls_ ? 30 : 0;
    controls_->setGeometry(0, 0, width(), ctrlH);

    if (selectedTrackId_ == 0 || !history_.contains(selectedTrackId_)) {
        p.setPen(QColor(100, 140, 100));
        p.setFont(QFont("Arial", 10));
        p.drawText(QRectF(0, ctrlH, width(), height() - ctrlH),
                   Qt::AlignCenter, "Select a track from the dropdown above.");
        return;
    }

    const TrackHistory &h = history_[selectedTrackId_];

    // Title
    p.setPen(QColor(200, 255, 200));
    p.setFont(QFont("Arial", 9, QFont::Bold));
    p.drawText(QPointF(4, ctrlH + 14),
               QString("Track #%1 — %2 samples")
               .arg(selectedTrackId_).arg(h.range.size()));

    // Divide remaining height among 6 channels
    int topY   = ctrlH + 20;
    int availH = height() - topY - 4;
    int chH    = availH / 6;

    struct Channel { const std::deque<double> *data; QString label; QColor color; };
    Channel channels[] = {
        { &h.range,     "Range (m)",   QColor(80, 200, 80)   },
        { &h.azimuth,   "Az (deg)",    QColor(80, 160, 255)  },
        { &h.elevation, "El (deg)",    QColor(255, 160, 80)  },
        { &h.rangeRate, "Rdot (m/s)",  QColor(255, 80, 80)   },
        { &h.speed,     "Speed (m/s)", QColor(200, 80, 255)  },
        { &h.quality,   "Quality",     QColor(255, 220, 80)  },
    };

    for (int i = 0; i < 6; ++i) {
        QRectF r(0, topY + i * chH, width(), chH - 1);
        drawChannel(p, r, *channels[i].data, channels[i].label, channels[i].color);
    }
}
