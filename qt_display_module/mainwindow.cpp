#include "mainwindow.h"
#include "udpreceiver.h"
#include "ppiwidget.h"
#include "scopewidget.h"
#include "timeserieswidget.h"
#include "logdialog.h"

#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QFont>
#include <QDateTime>
#include <cmath>

static constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Status / classification string helpers
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    setWindowTitle(tr("Counter-UAS Display Module"));
    resize(1280, 800);
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    // ── Toolbar ───────────────────────────────────────────────────────────
    QToolBar *tb = addToolBar(tr("Controls"));
    tb->setMovable(false);

    portLabel_ = new QLabel(tr("  Port: "), this);
    tb->addWidget(portLabel_);

    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1024, 65535);
    portSpin_->setValue(50001);
    tb->addWidget(portSpin_);

    startBtn_ = new QPushButton(tr("Start"), this);
    connect(startBtn_, &QPushButton::clicked, this, &MainWindow::onStartStop);
    tb->addWidget(startBtn_);

    tb->addSeparator();

    auto *logBtn = new QPushButton(tr("Log"), this);
    connect(logBtn, &QPushButton::clicked, this, &MainWindow::onShowLog);
    tb->addWidget(logBtn);

    // ── Tab widget ────────────────────────────────────────────────────────
    tabs_ = new QTabWidget(this);
    tabs_->setTabPosition(QTabWidget::North);
    setCentralWidget(tabs_);

    ppiWidget_ = new PPIWidget(this);
    tabs_->addTab(ppiWidget_, tr("PPI"));

    bScope_ = new ScopeWidget(ScopeWidget::BScope, this);
    tabs_->addTab(bScope_, tr("B-Scope"));

    cScope_ = new ScopeWidget(ScopeWidget::CScope, this);
    tabs_->addTab(cScope_, tr("C-Scope"));

    tsWidget_ = new TimeSeriesWidget(this);
    tabs_->addTab(tsWidget_, tr("Time Series"));

    buildTrackTable();
    tabs_->addTab(trackTable_, tr("Tracks"));

    buildClusterTable();
    tabs_->addTab(clusterTable_, tr("Clusters"));

    buildPredictedTable();
    tabs_->addTab(predictedTable_, tr("Predicted"));

    buildAssocTable();
    tabs_->addTab(assocTable_, tr("Association"));

    // ── Status bar ────────────────────────────────────────────────────────
    statusLabel_ = new QLabel(tr("Idle"), this);
    statusBar()->addPermanentWidget(statusLabel_);
}

static QTableWidget *makeTable(int cols, const QStringList &hdrs, QWidget *parent)
{
    auto *t = new QTableWidget(0, cols, parent);
    t->setHorizontalHeaderLabels(hdrs);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setEditTriggers(QTableWidget::NoEditTriggers);
    t->setSelectionBehavior(QTableWidget::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->setVisible(false);
    QFont f("Consolas", 9);
    f.setStyleHint(QFont::Monospace);
    t->setFont(f);
    return t;
}

void MainWindow::buildTrackTable()
{
    trackTable_ = makeTable(17, {
        "ID", "Status", "Class",
        "Range(m)", "Az(°)", "El(°)", "Rdot(m/s)",
        "X(m)", "Y(m)", "Z(m)",
        "Vx", "Vy", "Vz",
        "Quality", "Hits", "Miss", "Age"
    }, this);
}

void MainWindow::buildClusterTable()
{
    clusterTable_ = makeTable(11, {
        "ID", "#Dets",
        "Range(m)", "Az(°)", "El(°)",
        "Str(dBm)", "SNR(dB)", "RCS",
        "X(m)", "Y(m)", "Z(m)"
    }, this);
}

void MainWindow::buildAssocTable()
{
    assocTable_ = makeTable(4, {
        "Track ID", "Cluster ID", "Distance", "Status"
    }, this);
}

void MainWindow::buildPredictedTable()
{
    predictedTable_ = makeTable(19, {
        "Track ID", "Status",
        "Range(m)", "Az(°)", "El(°)",
        "X(m)", "Y(m)", "Z(m)",
        "Vx", "Vy", "Vz",
        "CovX", "CovY", "CovZ",
        "CV", "CA1", "CA2", "CTR1", "CTR2"
    }, this);
}

// ---------------------------------------------------------------------------
// Toolbar actions
// ---------------------------------------------------------------------------
void MainWindow::onStartStop()
{
    if (receiver_) {
        disconnectReceiver();
        delete receiver_;
        receiver_ = nullptr;
        msgCount_ = 0;
        startBtn_->setText(tr("Start"));
        portSpin_->setEnabled(true);
        statusLabel_->setText(tr("Stopped"));
        logLine(tr("Receiver stopped."));
        return;
    }

    quint16 port = static_cast<quint16>(portSpin_->value());
    receiver_ = new UdpReceiver(port, this);
    connectReceiver();

    if (!receiver_->bind()) {
        QMessageBox::warning(this, tr("Error"),
                             tr("Cannot bind to UDP port %1").arg(port));
        disconnectReceiver();
        delete receiver_;
        receiver_ = nullptr;
        return;
    }

    startBtn_->setText(tr("Stop"));
    portSpin_->setEnabled(false);
    statusLabel_->setText(tr("Listening on port %1 …").arg(port));
    logLine(tr("Receiver started on port %1.").arg(port));
}

void MainWindow::onShowLog()
{
    if (!logDialog_)
        logDialog_ = new LogDialog(this);
    logDialog_->show();
    logDialog_->raise();
    logDialog_->activateWindow();
}

// ---------------------------------------------------------------------------
// Connect / disconnect
// ---------------------------------------------------------------------------
void MainWindow::connectReceiver()
{
    connect(receiver_, &UdpReceiver::tracksReceived,
            this,      &MainWindow::onTracksReceived);
    connect(receiver_, &UdpReceiver::rawDetectionsReceived,
            this,      &MainWindow::onRawDetections);
    connect(receiver_, &UdpReceiver::clustersReceived,
            this,      &MainWindow::onClustersReceived);
    connect(receiver_, &UdpReceiver::assocReceived,
            this,      &MainWindow::onAssocReceived);
    connect(receiver_, &UdpReceiver::predictedReceived,
            this,      &MainWindow::onPredictedReceived);
}

void MainWindow::disconnectReceiver()
{
    if (receiver_) disconnect(receiver_, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------
// Slot helpers: fill one row in a QTableWidget
// ---------------------------------------------------------------------------
static void setCell(QTableWidget *tbl, int row, int &col, const QString &v,
                    Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter)
{
    auto *it = tbl->item(row, col);
    if (!it) {
        it = new QTableWidgetItem;
        it->setTextAlignment(align);
        tbl->setItem(row, col, it);
    }
    it->setText(v);
    ++col;
}

// ---------------------------------------------------------------------------
// Data handlers
// ---------------------------------------------------------------------------
void MainWindow::onTracksReceived(const QVector<TrackData> &tracks)
{
    ++msgCount_;

    ppiWidget_->setTracks(tracks);
    bScope_->setTracks(tracks);
    cScope_->setTracks(tracks);
    tsWidget_->updateTracks(tracks);

    trackTable_->setRowCount(tracks.size());
    for (int r = 0; r < tracks.size(); ++r) {
        const TrackData &t = tracks[r];
        int c = 0;
        setCell(trackTable_, r, c, QString::number(t.trackId));
        setCell(trackTable_, r, c, statusStr(t.status));
        setCell(trackTable_, r, c, classStr(t.classification));
        setCell(trackTable_, r, c, QString::number(t.range,              'f', 1));
        setCell(trackTable_, r, c, QString::number(t.azimuth * RAD2DEG,  'f', 2));
        setCell(trackTable_, r, c, QString::number(t.elevation * RAD2DEG,'f', 2));
        setCell(trackTable_, r, c, QString::number(t.rangeRate,           'f', 1));
        setCell(trackTable_, r, c, QString::number(t.x,                  'f', 1));
        setCell(trackTable_, r, c, QString::number(t.y,                  'f', 1));
        setCell(trackTable_, r, c, QString::number(t.z,                  'f', 1));
        setCell(trackTable_, r, c, QString::number(t.vx,                 'f', 1));
        setCell(trackTable_, r, c, QString::number(t.vy,                 'f', 1));
        setCell(trackTable_, r, c, QString::number(t.vz,                 'f', 1));
        setCell(trackTable_, r, c, QString::number(t.trackQuality,       'f', 2));
        setCell(trackTable_, r, c, QString::number(t.hitCount));
        setCell(trackTable_, r, c, QString::number(t.missCount));
        setCell(trackTable_, r, c, QString::number(t.age));
    }
    trackTable_->resizeColumnsToContents();

    updateStatusBar(tracks);
}

void MainWindow::onRawDetections(const RawDetectionFrame &frame)
{
    ppiWidget_->setDetections(frame);
    bScope_->setDetections(frame);
    cScope_->setDetections(frame);
}

void MainWindow::onClustersReceived(const QVector<ClusterData> &clusters,
                                    quint64 /*ts*/, quint32 /*dwellCount*/)
{
    clusterTable_->setRowCount(clusters.size());
    for (int r = 0; r < clusters.size(); ++r) {
        const ClusterData &cl = clusters[r];
        int c = 0;
        setCell(clusterTable_, r, c, QString::number(cl.clusterId));
        setCell(clusterTable_, r, c, QString::number(cl.numDetections));
        setCell(clusterTable_, r, c, QString::number(cl.range,              'f', 1));
        setCell(clusterTable_, r, c, QString::number(cl.azimuth * RAD2DEG,  'f', 2));
        setCell(clusterTable_, r, c, QString::number(cl.elevation * RAD2DEG,'f', 2));
        setCell(clusterTable_, r, c, QString::number(cl.strength,           'f', 1));
        setCell(clusterTable_, r, c, QString::number(cl.snr,               'f', 1));
        setCell(clusterTable_, r, c, QString::number(cl.rcs,               'f', 2));
        setCell(clusterTable_, r, c, QString::number(cl.x,                 'f', 1));
        setCell(clusterTable_, r, c, QString::number(cl.y,                 'f', 1));
        setCell(clusterTable_, r, c, QString::number(cl.z,                 'f', 1));
    }
    clusterTable_->resizeColumnsToContents();
}

void MainWindow::onAssocReceived(const QVector<AssocEntry> &entries,
                                 quint64 /*ts*/)
{
    assocTable_->setRowCount(entries.size());
    for (int r = 0; r < entries.size(); ++r) {
        const AssocEntry &e = entries[r];
        int c = 0;
        setCell(assocTable_, r, c,
                e.trackId   == 0xFFFFFFFFu ? QString("—") : QString::number(e.trackId));
        setCell(assocTable_, r, c,
                e.clusterId == 0xFFFFFFFFu ? QString("—") : QString::number(e.clusterId));
        setCell(assocTable_, r, c,
                e.distance < 0 ? QString("—") : QString::number(e.distance, 'f', 3));
        setCell(assocTable_, r, c,
                e.matched ? tr("Matched") : tr("Unmatched"),
                Qt::AlignLeft | Qt::AlignVCenter);
    }
    assocTable_->resizeColumnsToContents();
}

void MainWindow::onPredictedReceived(const QVector<PredictedEntry> &entries,
                                     quint64 /*ts*/)
{
    predictedTable_->setRowCount(entries.size());
    for (int r = 0; r < entries.size(); ++r) {
        const PredictedEntry &e = entries[r];
        int c = 0;
        setCell(predictedTable_, r, c, QString::number(e.trackId));
        setCell(predictedTable_, r, c, statusStr(e.trackStatus));
        setCell(predictedTable_, r, c, QString::number(e.range,              'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.azimuth * RAD2DEG,  'f', 2));
        setCell(predictedTable_, r, c, QString::number(e.elevation * RAD2DEG,'f', 2));
        setCell(predictedTable_, r, c, QString::number(e.x,   'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.y,   'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.z,   'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.vx,  'f', 2));
        setCell(predictedTable_, r, c, QString::number(e.vy,  'f', 2));
        setCell(predictedTable_, r, c, QString::number(e.vz,  'f', 2));
        setCell(predictedTable_, r, c, QString::number(e.covX,'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.covY,'f', 1));
        setCell(predictedTable_, r, c, QString::number(e.covZ,'f', 1));
        for (int m = 0; m < 5; ++m)
            setCell(predictedTable_, r, c, QString::number(e.modelProb[m], 'f', 3));
    }
    predictedTable_->resizeColumnsToContents();
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------
void MainWindow::updateStatusBar(const QVector<TrackData> &tracks)
{
    int conf = 0, tent = 0, coast = 0;
    for (const auto &t : tracks) {
        if      (t.status == 1) ++conf;
        else if (t.status == 0) ++tent;
        else if (t.status == 2) ++coast;
    }
    statusLabel_->setText(
        tr("Msgs: %1 | Tracks: %2  (Conf %3  Tent %4  Coast %5)")
            .arg(msgCount_)
            .arg(tracks.size())
            .arg(conf).arg(tent).arg(coast));
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
void MainWindow::logLine(const QString &text)
{
    if (!logDialog_)
        logDialog_ = new LogDialog(this);
    QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    logDialog_->appendEntry(QString("[%1] %2").arg(ts, text));
}
