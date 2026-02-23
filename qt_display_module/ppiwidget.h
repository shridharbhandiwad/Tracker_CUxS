#ifndef PPIWIDGET_H
#define PPIWIDGET_H

#include "udpreceiver.h"
#include <QWidget>
#include <QVector>
#include <QPointF>

class PPIWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PPIWidget(QWidget *parent = nullptr);

public slots:
    void setDetections(const RawDetectionFrame &frame);
    void setTracks(const QVector<TrackData> &tracks);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPointF toScreen(double x, double y) const;

    RawDetectionFrame dets_;
    QVector<TrackData> tracks_;

    double maxRange_  = 5000.0;  // metres
    double scale_     = 1.0;     // zoom multiplier
    QPointF panOffset_;          // pixels
    QPointF lastMouse_;
    bool    panning_  = false;
};

#endif // PPIWIDGET_H
