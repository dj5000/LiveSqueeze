#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>
#include <memory>

namespace lsqapp {

// Makes sure only one copy of the program runs per user. The first copy is the "primary": it
// listens on a local socket. A second copy asks the primary to show its window and exits.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);

    bool isPrimary() const { return primary_; }
    // For a secondary copy: asks the running copy to show itself. True if it answered.
    bool notifyPrimary(int timeoutMs = 1000);

signals:
    void showRequested();

private:
    QString key_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QLocalServer> server_;
    bool primary_ = false;
};

} // namespace lsqapp
