#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "udpreceiver.h"
#include <QMainWindow>
#include <QVector>

class QTabWidget;
class QTableWidget;
class QLabel;
class QSpinBox;
class QPushButton;
class UdpReceiver;
class PPIWidget;
class ScopeWidget;
class TimeSeriesWidget;
class LogDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    // UdpReceiver signals
    void onTracksReceived      (const QVector<TrackData>      &tracks);
    void onRawDetections       (const RawDetectionFrame       &frame);
    void onClustersReceived    (const QVector<ClusterData>    &clusters,
                                quint64 ts, quint32 dwellCount);
    void onAssocReceived       (const QVector<AssocEntry>     &entries,
                                quint64 ts);
    void onPredictedReceived   (const QVector<PredictedEntry> &entries,
                                quint64 ts);

    // Toolbar
    void onStartStop();
    void onShowLog();

private:
    void buildUi();
    void buildTrackTable();
    void buildClusterTable();
    void buildAssocTable();
    void buildPredictedTable();
    void connectReceiver();
    void disconnectReceiver();
    void updateStatusBar(const QVector<TrackData> &tracks);
    void logLine(const QString &text);

    // Tabs
    QTabWidget      *tabs_            = nullptr;
    PPIWidget       *ppiWidget_       = nullptr;
    ScopeWidget     *bScope_          = nullptr;
    ScopeWidget     *cScope_          = nullptr;
    TimeSeriesWidget*tsWidget_        = nullptr;
    QTableWidget    *trackTable_      = nullptr;
    QTableWidget    *clusterTable_    = nullptr;
    QTableWidget    *assocTable_      = nullptr;
    QTableWidget    *predictedTable_  = nullptr;

    // Toolbar controls
    QLabel          *portLabel_       = nullptr;
    QSpinBox        *portSpin_        = nullptr;
    QPushButton     *startBtn_        = nullptr;
    QLabel          *statusLabel_     = nullptr;

    // Log dialog (created on demand)
    LogDialog       *logDialog_       = nullptr;

    // Networking
    UdpReceiver     *receiver_        = nullptr;
    quint64          msgCount_        = 0;
};

#endif // MAINWINDOW_H
