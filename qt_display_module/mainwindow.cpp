#include "mainwindow.h"
#include "udpreceiver.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QFont>
#include <cmath>

static constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;

static const char *statusStr(uint32_t s)
{
    switch (s) {
    case 0:  return "TENT";
    case 1:  return "CONF";
    case 2:  return "COAST";
    case 3:  return "DEL";
    default: return "???";
    }
}

static const char *classStr(uint32_t c)
{
    switch (c) {
    case 0:  return "Unknown";
    case 1:  return "Drone-R";
    case 2:  return "Drone-F";
    case 3:  return "Bird";
    case 4:  return "Clutter";
    default: return "???";
    }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    setWindowTitle(tr("Counter-UAS Display Module"));
    resize(1000, 500);
}

void MainWindow::buildUi()
{
    // --- toolbar ---
    QToolBar *toolbar = addToolBar(tr("Controls"));
    toolbar->setMovable(false);

    toolbar->addWidget(new QLabel(tr("  Port: ")));
    portSpin_ = new QSpinBox;
    portSpin_->setRange(1024, 65535);
    portSpin_->setValue(50001);
    toolbar->addWidget(portSpin_);

    startBtn_ = new QPushButton(tr("Start"));
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartStop);
    toolbar->addWidget(startBtn_);

    // --- track table ---
    const QStringList headers = {
        "ID", "Status", "Class",
        "Range(m)", "Az(deg)", "El(deg)", "Rdot(m/s)",
        "X(m)", "Y(m)", "Z(m)",
        "Quality", "Hits", "Miss", "Age"
    };

    table_ = new QTableWidget(0, headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setEditTriggers(QTableWidget::NoEditTriggers);
    table_->setSelectionBehavior(QTableWidget::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);

    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    table_->setFont(monoFont);

    setCentralWidget(table_);

    // --- status bar ---
    statusLabel_ = new QLabel(tr("Idle"));
    statusBar()->addPermanentWidget(statusLabel_);
}

void MainWindow::onStartStop()
{
    if (receiver_) {
        delete receiver_;
        receiver_ = nullptr;
        msgCount_ = 0;
        startBtn_->setText(tr("Start"));
        portSpin_->setEnabled(true);
        statusLabel_->setText(tr("Stopped"));
        return;
    }

    quint16 port = static_cast<quint16>(portSpin_->value());
    receiver_ = new UdpReceiver(port, this);

    connect(receiver_, &UdpReceiver::tracksReceived,
            this,      &MainWindow::onTracksReceived);

    if (!receiver_->bind()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot bind to port %1").arg(port));
        delete receiver_;
        receiver_ = nullptr;
        return;
    }

    startBtn_->setText(tr("Stop"));
    portSpin_->setEnabled(false);
    statusLabel_->setText(tr("Listening on port %1 ...").arg(port));
}

void MainWindow::onTracksReceived(const QVector<TrackData> &tracks)
{
    ++msgCount_;

    table_->setRowCount(tracks.size());
    for (int r = 0; r < tracks.size(); ++r) {
        const TrackData &t = tracks[r];
        int c = 0;
        auto setCell = [&](const QString &text) {
            auto *item = table_->item(r, c);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                table_->setItem(r, c, item);
            }
            item->setText(text);
            ++c;
        };

        setCell(QString::number(t.trackId));
        setCell(statusStr(t.status));
        setCell(classStr(t.classification));
        setCell(QString::number(t.range,       'f', 1));
        setCell(QString::number(t.azimuth   * RAD2DEG, 'f', 2));
        setCell(QString::number(t.elevation * RAD2DEG, 'f', 2));
        setCell(QString::number(t.rangeRate,   'f', 1));
        setCell(QString::number(t.x,           'f', 1));
        setCell(QString::number(t.y,           'f', 1));
        setCell(QString::number(t.z,           'f', 1));
        setCell(QString::number(t.trackQuality,'f', 2));
        setCell(QString::number(t.hitCount));
        setCell(QString::number(t.missCount));
        setCell(QString::number(t.age));
    }

    table_->resizeColumnsToContents();
    updateStatusBar(tracks);
}

void MainWindow::updateStatusBar(const QVector<TrackData> &tracks)
{
    int conf = 0, tent = 0, coast = 0;
    for (const auto &t : tracks) {
        if (t.status == 1) ++conf;
        else if (t.status == 0) ++tent;
        else if (t.status == 2) ++coast;
    }

    statusLabel_->setText(
        tr("Msgs: %1 | Tracks: %2 (Conf %3  Tent %4  Coast %5)")
            .arg(msgCount_)
            .arg(tracks.size())
            .arg(conf).arg(tent).arg(coast));
}
