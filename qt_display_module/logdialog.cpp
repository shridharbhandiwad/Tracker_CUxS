#include "logdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>

LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Event Log"));
    resize(760, 420);
    // Allow it to be minimized / maximized as a normal window
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    QFont mono("Consolas", 9);
    mono.setStyleHint(QFont::Monospace);
    log_->setFont(mono);
    log_->setStyleSheet("QPlainTextEdit { background:#0a0f0a; color:#a0ffa0; }");

    autoScrollCb_ = new QCheckBox(tr("Auto-scroll"), this);
    autoScrollCb_->setChecked(true);

    auto *clearBtn = new QPushButton(tr("Clear"), this);
    connect(clearBtn, &QPushButton::clicked, this, &LogDialog::clear);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(autoScrollCb_);
    btnRow->addStretch();
    btnRow->addWidget(clearBtn);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(log_);
    layout->addLayout(btnRow);
}

void LogDialog::appendEntry(const QString &text)
{
    log_->appendPlainText(text);
    if (autoScrollCb_->isChecked())
        log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void LogDialog::clear()
{
    log_->clear();
}
