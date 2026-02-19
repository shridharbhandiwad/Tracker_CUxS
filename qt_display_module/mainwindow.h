#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

class QTableWidget;
class QLabel;
class QSpinBox;
class QPushButton;
class UdpReceiver;
struct TrackData;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onTracksReceived(const QVector<TrackData> &tracks);
    void onStartStop();

private:
    void buildUi();
    void updateStatusBar(const QVector<TrackData> &tracks);

    QTableWidget *table_      = nullptr;
    QLabel       *statusLabel_ = nullptr;
    QSpinBox     *portSpin_   = nullptr;
    QPushButton  *startBtn_   = nullptr;

    UdpReceiver  *receiver_   = nullptr;
    quint64       msgCount_   = 0;
};

#endif // MAINWINDOW_H
