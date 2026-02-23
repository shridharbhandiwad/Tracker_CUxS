#ifndef TIMESERIESWIDGET_H
#define TIMESERIESWIDGET_H

#include "udpreceiver.h"
#include <QWidget>
#include <QVector>
#include <QMap>
#include <QComboBox>
#include <QLabel>
#include <deque>

struct TrackHistory {
    std::deque<double>  range, azimuth, elevation, rangeRate, quality, speed;
    std::deque<quint64> timestamps;
};

class TimeSeriesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimeSeriesWidget(QWidget *parent = nullptr);

    // Returns the internal control widget (combo + label) to embed in a toolbar
    QWidget *controlWidget() { return controls_; }

public slots:
    void updateTracks(const QVector<TrackData> &tracks);

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onTrackChanged(int index);

private:
    void drawChannel(QPainter &p, const QRectF &rect,
                     const std::deque<double> &data,
                     const QString &label, const QColor &color) const;

    QMap<uint32_t, TrackHistory> history_;
    uint32_t selectedTrackId_ = 0;

    static constexpr int MAX_HISTORY = 200;

    // Controls embedded in the tab header
    QWidget   *controls_;
    QComboBox *trackCombo_;
};

#endif // TIMESERIESWIDGET_H
