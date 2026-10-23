#include "services/background_tasks.h"

#include <algorithm>

namespace ot {

BackgroundTasks& BackgroundTasks::shared() {
    static BackgroundTasks tasks;
    return tasks;
}

quint64 BackgroundTasks::start(const QString& title, std::function<void()> cancel) {
    Task t;
    t.id = next_++;
    t.title = title;
    t.cancel = std::move(cancel);
    tasks_.push_back(std::move(t));
    emit changed();
    return tasks_.back().id;
}

void BackgroundTasks::setProgress(quint64 id, int percent) {
    for (Task& t : tasks_) {
        if (t.id == id) {
            const int p = percent < 0 ? -1 : std::min(percent, 100);
            if (t.progress != p) {
                t.progress = p;
                emit changed();
            }
            return;
        }
    }
}

void BackgroundTasks::setTitle(quint64 id, const QString& title) {
    for (Task& t : tasks_) {
        if (t.id == id && t.title != title) {
            t.title = title;
            emit changed();
            return;
        }
    }
}

void BackgroundTasks::finish(quint64 id) {
    const auto it = std::find_if(tasks_.begin(), tasks_.end(), [id](const Task& t) { return t.id == id; });
    if (it != tasks_.end()) {
        tasks_.erase(it);
        emit changed();
    }
}

void BackgroundTasks::cancel(quint64 id) {
    for (const Task& t : tasks_) {
        if (t.id == id) {
            if (t.cancel) {
                const std::function<void()> fn = t.cancel;
                fn();
            }
            return;
        }
    }
}

}  // namespace ot
