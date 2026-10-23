#include "support/qt_app.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QThread>

namespace ot::test {

QCoreApplication& qtApp() {
    static int argc = 1;
    static char name[] = "unit_tests";
    static char* argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    return app;
}

bool waitUntil(const std::function<bool()>& pred, int timeoutMs) {
    qtApp();
    const QDeadlineTimer deadline(timeoutMs);
    while (!pred()) {
        if (deadline.hasExpired()) {
            return pred();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

}  // namespace ot::test
