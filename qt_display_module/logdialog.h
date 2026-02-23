#ifndef LOGDIALOG_H
#define LOGDIALOG_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QCheckBox>

// Non-modal floating window that accumulates event log lines.
// Callers use appendEntry() to add lines; the dialog scrolls automatically
// when the user enables auto-scroll.
class LogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LogDialog(QWidget *parent = nullptr);

    // Append one formatted line to the log.
    void appendEntry(const QString &text);

public slots:
    void clear();

private:
    QPlainTextEdit *log_;
    QCheckBox      *autoScrollCb_;
};

#endif // LOGDIALOG_H
