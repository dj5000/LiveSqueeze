#include "SingleInstance.hpp"

#include <QDir>
#include <QLocalSocket>

namespace lsqapp {

SingleInstance::SingleInstance(const QString& key, QObject* parent) : QObject(parent), key_(key) {
    lock_ = std::make_unique<QLockFile>(QDir::temp().filePath(key + QStringLiteral(".lock")));
    lock_->setStaleLockTime(0); // a lock left by a crashed copy is detected through its process id
    if (!lock_->tryLock(0)) {
        return;
    }
    primary_ = true;
    server_ = std::make_unique<QLocalServer>();
    QLocalServer::removeServer(key); // a socket file left by a crashed copy
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (server_->listen(key)) {
        connect(server_.get(), &QLocalServer::newConnection, this, [this] {
            while (QLocalSocket* s = server_->nextPendingConnection()) {
                s->deleteLater();
                emit showRequested();
            }
        });
    }
}

bool SingleInstance::notifyPrimary(int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(key_);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }
    socket.write("show\n");
    socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return true;
}

} // namespace lsqapp
