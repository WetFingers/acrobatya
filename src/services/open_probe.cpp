#include "services/open_probe.h"

#include <QElapsedTimer>
#include <QFuture>
#include <QThread>

#include <algorithm>
#include <exception>
#include <vector>

#include "core/mu/error.h"
#include "core/render/display_cache.h"
#include "core/render/renderer.h"
#include "services/document_session.h"
#include "services/task_context.h"

namespace ot {

namespace {

constexpr int kThumb = 32;

// Дождаться задачи не дольше timeoutMs; false — срок вышел (задачи отменены).
template <class T>
bool waitFor(DocumentSession& session, const QFuture<T>& future, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!future.isFinished()) {
        if (t.elapsed() > timeoutMs) {
            session.cancelAll();
            QFuture<T>(future).waitForFinished();
            return false;
        }
        QThread::msleep(2);
    }
    return true;
}

OpenProbe refused(OpenProbe p, QString reason) {
    p.result = OpenProbe::Result::Refused;
    p.reason = std::move(reason);
    return p;
}

OpenProbe failed(OpenProbe p, QString reason) {
    p.result = OpenProbe::Result::Failed;
    p.reason = std::move(reason);
    return p;
}

// Ошибка MuPDF или наша: по коду и виду (TaskFailed / MuError).
OpenProbe classify(OpenProbe p, int code, ErrorKind kind, const QString& message) {
    if (code == 0) {
        return failed(std::move(p), QStringLiteral("our code: ") + message);  // исключение не из MuPDF
    }
    if (kind == ErrorKind::Argument) {
        return failed(std::move(p), QStringLiteral("argument: ") + message);
    }
    return refused(std::move(p),
                   QString::fromLatin1(MuError::codeName(code)) + QStringLiteral(": ") + message);
}

}  // namespace

OpenProbe probeOpen(const QString& path, int timeoutMs) {
    OpenProbe probe;
    DocumentSession session;
    const QFuture<OpenSummary> opened = session.openAsync(path);
    if (!waitFor(session, opened, timeoutMs)) {
        return refused(probe, QStringLiteral("timeout while opening"));
    }
    OpenSummary summary;
    try {
        summary = opened.result();
    } catch (const std::exception& e) {
        return failed(probe, QStringLiteral("our code: ") + QString::fromUtf8(e.what()));
    }
    if (!summary.ok) {
        if (summary.needsPassword) {
            return refused(probe, QStringLiteral("password"));
        }
        if (summary.error.kind == ErrorKind::NotFound) {
            return refused(probe, QStringLiteral("not found: ") + summary.error.message);
        }
        return classify(probe, summary.error.code, summary.error.kind, summary.error.message);
    }
    probe.pages = summary.pageCount;
    probe.repaired = summary.repaired;
    probe.result = OpenProbe::Result::Opened;
    if (summary.pageCount == 0) {
        session.close();
        return probe;
    }
    // Первая страница в 32 × 32 (как зонд engine-тестов), отмена — по сроку.
    const QFuture<bool> drawn = session.post(
        [](TaskContext& tc) {
            const std::shared_ptr<const DisplayList> list = tc.displayList(0);
            const RectF b = list->bounds();
            const float side = std::max(b.width(), b.height());
            if (side <= 0) {
                return false;
            }
            const float s = static_cast<float>(kThumb) / side;
            const Matrix m = concat(Matrix::translate(-b.x0, -b.y0), Matrix::scale(s));
            std::vector<std::uint8_t> pixels(static_cast<size_t>(kThumb) * kThumb * 4);
            renderDisplayList(tc.context(), *list, m, IRect{0, 0, kThumb, kThumb}, pixels.data(), kThumb * 4,
                              PixelFormat::Bgra32Premultiplied, tc.cookie());
            return true;
        },
        TaskPriority::Normal, QStringLiteral("open-all probe"));
    if (!waitFor(session, drawn, timeoutMs)) {
        session.close();
        return refused(probe, QStringLiteral("timeout while rendering page 1"));
    }
    try {
        probe.rendered = drawn.result();
    } catch (const MuError& e) {
        // Файл открылся, а страница 1 битая (и дерево страниц: «invalid page number» — FZ_ERROR_ARGUMENT от
        // MuPDF на испорченном /Pages): окно рисует «Страница не отрисована» — это ответ на файл, не сбой.
        session.close();
        probe.reason = QStringLiteral("page 1: ") + QString::fromLatin1(MuError::codeName(e.code())) +
                       QStringLiteral(": ") + QString::fromUtf8(e.what());
        return probe;
    } catch (const std::exception& e) {
        session.close();
        return failed(probe, QStringLiteral("our code (render): ") + QString::fromUtf8(e.what()));
    }
    session.close();
    return probe;
}

}  // namespace ot
