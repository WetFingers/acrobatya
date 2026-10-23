#include "services/memory_guard.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>

#include <spdlog/spdlog.h>

#include "core/mu/context.h"
#include "services/tile_cache.h"

namespace ot {

MemoryGuard::MemoryGuard(QObject* parent, int idleMs) : QObject(parent) {
    idle_.setSingleShot(true);
    idle_.setInterval(idleMs);
    connect(&idle_, &QTimer::timeout, this, &MemoryGuard::relieve);
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
        if (auto* gui = qobject_cast<QGuiApplication*>(app)) {
            connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationInactive || state == Qt::ApplicationHidden ||
                    state == Qt::ApplicationSuspended) {
                    relieve();
                }
            });
        }
    }
    idle_.start();
}

MemoryGuard::~MemoryGuard() {
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->removeEventFilter(this);
    }
}

void MemoryGuard::setIdleInterval(int ms) {
    idle_.setInterval(ms);
    idle_.start();
}

void MemoryGuard::relieve() {
    ++relieved_;
    // Store общий у всех клонов и под замком: сжимаем клоном GUI-потока.
    const bool fit = MuContext::forThisThread().shrinkStore(kShrinkPercent);
    if (tiles_ != nullptr) {
        tiles_->shrink(static_cast<int>(kShrinkPercent));
    }
    spdlog::debug("low memory: MuPDF store shrunk to {}% ({})", kShrinkPercent,
                  fit ? "fits" : "items in use");
    emit lowMemory();
}

void MemoryGuard::noteActivity() { idle_.start(); }

bool MemoryGuard::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::ApplicationDeactivate:
            relieve();
            break;
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
        case QEvent::TabletPress:
            if (!idle_.isActive() || idle_.remainingTime() < idle_.interval() - 1000) {
                idle_.start();  // не чаще раза в секунду: MouseMove приходит пачками
            }
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace ot
