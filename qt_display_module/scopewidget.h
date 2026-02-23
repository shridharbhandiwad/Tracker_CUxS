#ifndef SCOPEWIDGET_H
#define SCOPEWIDGET_H

#include "udpreceiver.h"
#include <QWidget>
#include <QVector>

class ScopeWidget : public QWidget
{
    Q_OBJECT
public:
    enum Mode { BScope, CScope };

    explicit ScopeWidget(Mode mode, QWidget *parent = nullptr);

public slots:
    void setDetections(const RawDetectionFrame &frame);
    void setTracks(const QVector<TrackData> &tracks);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawGrid(QPainter &p, const QRectF &plot) const;
    void drawAxesLabels(QPainter &p, const QRectF &plot) const;
    QPointF dataToPlot(double hVal, double vVal, const QRectF &plot) const;

    Mode mode_;
    RawDetectionFrame dets_;
    QVector<TrackData> tracks_;

    double maxRange_ = 5000.0; // for B-scope Y axis
};

#endif // SCOPEWIDGET_H
