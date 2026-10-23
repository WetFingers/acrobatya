// RenderPool — общий на процесс пул рендера тайлов (эталон 4.1–4.2): QThreadPool с maxThreadCount =
// idealThreadCount() − 2; каждый поток рисует своим клоном контекста (MuContext::forThisThread()):
// fz_run_display_list(list, dev, ctm, scissor = тайл, &cookie) прямо в память QImage
// (Format_ARGB32_Premultiplied, fz_new_pixmap_with_data — без копий), setDevicePixelRatio(dpr).
// Результат — сигнал tileReady (из потока пула; получателю в GUI-потоке — QueuedConnection) и, если задан,
// колбэк в потоке объекта context (QMetaObject::invokeMethod, Qt::QueuedConnection). Отмена: fz_cookie
// задачи (abort = 1 ставит GUI-поток), ещё не начатая задача снимается с очереди сразу; результат отменённой
// приходит с aborted = true и пустым растром. Приоритет: QThreadPool::start(job, priority), видимым — выше
// (retarget при смене зума: невидимым abort = 1, видимым — приоритет). Списки — только документов, открытых
// на клонах MuContext::global() (DocumentSession так и открывает): иначе результат — ошибка.
#pragma once

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QThreadPool>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "core/render/tiles.h"
#include "core/util/geometry.h"
#include "services/document_session.h"

namespace ot {

class DisplayList;

struct TileRequest {
    TileKey key;
    std::shared_ptr<const DisplayList> list;  // из DisplayCache документа (DocumentSession::displayList)
    Matrix matrix;                            // pageMatrix(list->bounds(), zoom, dpr, rotation)
    IRect rect;                               // тайл в device px этой матрицы
    qreal dpr = 1.0;                          // QImage::setDevicePixelRatio
    int aa = -1;                              // сглаживание 0–8; −1 — как у контекста
    int priority = 0;                         // больше — раньше (видимые тайлы)
};

struct TileResult {
    TileKey key;
    IRect rect;
    QImage image;          // пусто, если отменён или ошибка
    bool aborted = false;  // отменён (cookie или снят с очереди)
    TaskFailed error;      // code != 0 или kind != None — ошибка рендера
    bool ok() const noexcept { return !image.isNull(); }
};

class RenderPool : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const TileResult&)>;

    // threads ≤ 0 — idealThreadCount() − 2 (не меньше 1).
    explicit RenderPool(int threads = 0, QObject* parent = nullptr);
    // Отменяет всё и ждёт потоки.
    ~RenderPool() override;

    RenderPool(const RenderPool&) = delete;
    RenderPool& operator=(const RenderPool&) = delete;

    int maxThreads() const;

    // Поставить тайл. id задачи — для cancel/setPriority; результат — tileReady и done (в потоке context).
    quint64 submit(TileRequest request, QObject* context = nullptr, Callback done = {});

    // Отменить задачу / все задачи документа / всё: не начатые снимаются с очереди, у идущих abort = 1.
    void cancel(quint64 job);
    void cancelDocument(std::uint64_t doc);
    void cancelAll();
    // Смена зума и прокрутки: задачам документа, для которых keep(key) == false, — abort = 1; остальным
    // (видимым) — приоритет visiblePriority.
    void retarget(std::uint64_t doc, const std::function<bool(const TileKey&)>& keep, int visiblePriority);
    // Поднять приоритет ещё не начатой задачи; false — уже идёт или нет такой.
    bool setPriority(quint64 job, int priority);

    // Задач в очереди и в работе.
    int pending() const;
    bool waitForDone(int msecs = -1);

signals:
    void tileReady(const ot::TileResult& result);

private:
    class Job;
    friend class Job;

    std::shared_ptr<Job> find(quint64 id) const;
    void finished(quint64 id);
    void deliver(const std::shared_ptr<Job>& job, TileResult result);
    void abortJob(const std::shared_ptr<Job>& job);

    QThreadPool pool_;
    mutable std::mutex mutex_;
    std::map<quint64, std::shared_ptr<Job>> jobs_;
    quint64 nextId_ = 1;
};

}  // namespace ot

Q_DECLARE_METATYPE(ot::TileResult)
