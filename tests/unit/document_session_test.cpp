// unit.document_session.*, unit.document_registry.* — поток документа (эталон 4.1–4.2): задача идёт в потоке
// сессии, результат — через QFuture, ошибка — исключение в QFuture и сигнал taskFailed; приоритеты очереди;
// cancelAll (очередь — FZ_ERROR_ABORT, выполняемый рендер прерывается через fz_cookie за < 200 мс); правки
// (revision, documentChanged, pageChanged); два документа — два потока, не блокируют друг друга (view-27).
// Без виджетов: QCoreApplication из support/qt_app.h, сигналы — DirectConnection или цикл событий.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QThread>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/render/renderer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_registry.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

using ot::DocumentRegistry;
using ot::DocumentSession;
using ot::ErrorKind;
using ot::MuError;
using ot::TaskContext;
using ot::TaskFailed;
using ot::TaskPriority;

namespace fs = std::filesystem;

namespace {

// Файлы тестов: n-страничный PDF и «тяжёлая» страница во временном каталоге с кириллицей.
class Files {
public:
    fs::path pdf(int pages, const std::wstring& name = L"док") {
        const fs::path p = dir_.path() / (name + std::to_wstring(pages) + L".pdf");
        if (!fs::exists(p)) {
            ot::MuContext mu;
            ot::paths::writeFile(p, ot::test::makePdf(mu, pages));
        }
        return p;
    }
    fs::path heavy() {
        const fs::path p = dir_.path() / L"тяжёлая.pdf";
        if (!fs::exists(p)) {
            ot::MuContext mu;
            ot::paths::writeFile(p, ot::test::makeHeavyPdf(mu));
        }
        return p;
    }

private:
    ot::test::TempDir dir_;
};

QString qpath(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

// Ошибка из QFuture: вид MuError или Other.
template <class T>
ErrorKind futureErrorKind(QFuture<T>& f) {
    try {
        f.waitForFinished();
        if constexpr (!std::is_void_v<T>) {
            (void)f.result();
        }
    } catch (const MuError& e) {
        return e.kind();
    } catch (...) {
        return ErrorKind::Other;
    }
    return ErrorKind::None;
}

}  // namespace

TEST(document_session, task_runs_in_session_thread_and_result_comes_through_future) {
    ot::test::qtApp();
    DocumentSession s;
    EXPECT_NE(s.threadId(), std::this_thread::get_id());
    EXPECT_FALSE(s.isSessionThread());
    QFuture<std::thread::id> f = s.post([](TaskContext&) { return std::this_thread::get_id(); });
    EXPECT_EQ(f.result(), s.threadId());
    // void-задача и результат-строка
    std::atomic<int> ran{0};
    QFuture<void> v = s.post([&](TaskContext& tc) {
        EXPECT_TRUE(tc.session().isSessionThread());
        EXPECT_FALSE(tc.hasDocument());
        ran = 1;
    });
    v.waitForFinished();
    EXPECT_EQ(ran.load(), 1);
    EXPECT_EQ(s.post([](TaskContext&) { return std::string("ok"); }).result(), "ok");
    EXPECT_EQ(s.postSync([](TaskContext&) { return 7; }), 7);
}

TEST(document_session, open_reports_pages_sizes_and_renders) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    std::atomic<int> openedCount{0};
    QObject::connect(&s, &DocumentSession::opened, &s, [&] { ++openedCount; }, Qt::DirectConnection);
    ASSERT_TRUE(s.open(qpath(files.pdf(3)))) << s.errorString().toStdString();
    EXPECT_EQ(openedCount.load(), 1);
    EXPECT_TRUE(s.isOpen());
    EXPECT_EQ(s.pageCount(), 3);
    EXPECT_EQ(s.pageSizePoints(2), QSizeF(595, 842));
    EXPECT_FALSE(s.pageSizePoints(3).isValid());
    const ot::OpenSummary sum = s.summary();
    EXPECT_TRUE(sum.ok);
    EXPECT_TRUE(sum.isPdf);
    EXPECT_FALSE(sum.repaired);
    // Растр 72 dpi: ARGB32_Premultiplied, чёрный квадрат 100..200 pt от левого верхнего угла.
    const QImage img = s.renderPage(0, 72);
    ASSERT_FALSE(img.isNull()) << s.errorString().toStdString();
    EXPECT_EQ(img.format(), QImage::Format_ARGB32_Premultiplied);
    EXPECT_EQ(img.size(), QSize(595, 842));
    EXPECT_EQ(img.pixel(150, 150), qRgb(0, 0, 0));
    EXPECT_EQ(img.pixel(50, 50), qRgb(255, 255, 255));
    // Задача видит тот же документ, PageHandle — страницу на время задачи.
    const float width = s.postSync([](TaskContext& tc) {
        const ot::PageHandle page = tc.page(1);
        EXPECT_EQ(page.index(), 1);
        EXPECT_NE(page.get(), nullptr);
        fz_context* ctx = tc.context();
        return fz_bound_page(ctx, page.get()).x1;
    });
    EXPECT_FLOAT_EQ(width, 595.0f);
    std::atomic<int> closedCount{0};
    QObject::connect(&s, &DocumentSession::closed, &s, [&] { ++closedCount; }, Qt::DirectConnection);
    s.close();
    EXPECT_EQ(closedCount.load(), 1);
    EXPECT_FALSE(s.isOpen());
    EXPECT_EQ(s.pageCount(), 0);
}

TEST(document_session, open_failure_is_summary_not_exception) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    const ot::OpenSummary missing = s.openAsync(qpath(files.pdf(1).parent_path() / L"нет.pdf")).result();
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(missing.error.kind, ErrorKind::NotFound);
    EXPECT_FALSE(s.open(qpath(ot::test::sourceDir() / L"tests/data/mini/enc-r6.pdf")));
    EXPECT_TRUE(s.summary().needsPassword);
    EXPECT_FALSE(s.isOpen());
}

TEST(document_session, error_becomes_future_exception_and_task_failed_signal) {
    ot::test::qtApp();
    DocumentSession s;
    std::mutex m;
    std::vector<TaskFailed> failures;
    QObject::connect(
        &s, &DocumentSession::taskFailed, &s,
        [&](const TaskFailed& f) {
            const std::lock_guard lock(m);
            failures.push_back(f);
        },
        Qt::DirectConnection);
    QFuture<int> f = s.post([](TaskContext&) -> int { throw MuError(FZ_ERROR_FORMAT, "broken xref"); },
                            TaskPriority::Normal, QStringLiteral("load"));
    EXPECT_EQ(futureErrorKind(f), ErrorKind::Damaged);
    // Без документа задача, которой он нужен, — logic_error (вид Other).
    QFuture<void> g = s.post([](TaskContext& tc) { (void)tc.document(); });
    EXPECT_EQ(futureErrorKind(g), ErrorKind::Other);
    s.postSync([](TaskContext&) {});  // сигналы предыдущих задач уже испущены
    const std::lock_guard lock(m);
    ASSERT_EQ(failures.size(), 2u);
    EXPECT_EQ(failures[0].task, QStringLiteral("load"));
    EXPECT_EQ(failures[0].code, FZ_ERROR_FORMAT);
    EXPECT_EQ(failures[0].kind, ErrorKind::Damaged);
    EXPECT_EQ(failures[0].message, QStringLiteral("broken xref"));
    EXPECT_EQ(failures[0].hint, QStringLiteral("open-repaired"));
    EXPECT_EQ(failures[1].kind, ErrorKind::Other);
    EXPECT_EQ(failures[1].hint, QStringLiteral("report"));
}

TEST(document_session, task_failed_reaches_gui_thread_queued) {
    ot::test::qtApp();
    DocumentSession s;
    QObject receiver;  // живёт в главном потоке
    std::thread::id deliveredIn;
    int code = 0;
    QObject::connect(&s, &DocumentSession::taskFailed, &receiver, [&](const TaskFailed& f) {
        deliveredIn = std::this_thread::get_id();
        code = f.code;
    });
    (void)s.post([](TaskContext&) { throw MuError(FZ_ERROR_UNSUPPORTED, "no xfa"); });
    ASSERT_TRUE(ot::test::waitUntil([&] { return code != 0; }));
    EXPECT_EQ(code, FZ_ERROR_UNSUPPORTED);
    EXPECT_EQ(deliveredIn, std::this_thread::get_id());
}

TEST(document_session, priorities_order_queued_tasks) {
    ot::test::qtApp();
    DocumentSession s;
    std::latch gate(1);
    std::latch started(1);
    std::mutex m;
    std::vector<std::string> order;
    auto record = [&](const char* name) {
        return [&, name](TaskContext&) {
            const std::lock_guard lock(m);
            order.emplace_back(name);
        };
    };
    (void)s.post([&](TaskContext&) {  // поток занят, остальное ждёт в очереди
        started.count_down();
        gate.wait();
    });
    started.wait();
    (void)s.post(record("low1"), TaskPriority::Low);
    (void)s.post(record("normal1"), TaskPriority::Normal);
    (void)s.post(record("low2"), TaskPriority::Low);
    (void)s.post(record("urgent"), TaskPriority::Urgent);
    (void)s.post(record("background"), TaskPriority::Background);
    (void)s.edit(QStringLiteral("edit"), record("edit"));  // правка — обычный приоритет, по порядку
    (void)s.post(record("high"), TaskPriority::High);
    EXPECT_EQ(s.pendingTasks(), 8);
    gate.count_down();
    s.postSync([](TaskContext&) {}, TaskPriority::Background);
    const std::vector<std::string> expected = {"urgent", "high", "normal1",   "edit",
                                               "low1",   "low2", "background"};
    EXPECT_EQ(order, expected);
    EXPECT_EQ(s.pendingTasks(), 0);
}

TEST(document_session, cancel_all_aborts_queued_tasks) {
    ot::test::qtApp();
    DocumentSession s;
    std::latch gate(1);
    std::latch started(1);
    std::atomic<int> ran{0};
    QFuture<void> blocker = s.post([&](TaskContext& tc) {
        started.count_down();
        gate.wait();
        EXPECT_TRUE(tc.aborted());  // cancelAll пришёл, пока задача шла
    });
    std::vector<QFuture<int>> queued;
    for (int i = 0; i < 5; ++i) {
        queued.push_back(s.post([&](TaskContext&) { return ++ran; }));
    }
    std::atomic<int> failed{0};
    QObject::connect(&s, &DocumentSession::taskFailed, &s, [&] { ++failed; }, Qt::DirectConnection);
    started.wait();
    s.cancelAll();
    gate.count_down();
    blocker.waitForFinished();
    for (auto& f : queued) {
        EXPECT_EQ(futureErrorKind(f), ErrorKind::Aborted);
    }
    EXPECT_EQ(ran.load(), 0);
    EXPECT_EQ(failed.load(), 0);  // отмена — не ошибка для интерфейса
    // Сессия жива и принимает новые задачи.
    EXPECT_EQ(s.postSync([](TaskContext&) { return 5; }), 5);
}

TEST(document_session, cancelled_future_skips_task) {
    ot::test::qtApp();
    DocumentSession s;
    std::latch gate(1);
    std::atomic<int> ran{0};
    (void)s.post([&](TaskContext&) { gate.wait(); });
    QFuture<void> f = s.post([&](TaskContext&) { ++ran; });
    f.cancel();
    gate.count_down();
    s.postSync([](TaskContext&) {});
    EXPECT_EQ(ran.load(), 0);
    EXPECT_TRUE(f.isCanceled());
}

TEST(document_session, cookie_cancel_stops_long_render_within_200ms) {
    OT_FEATURE("view-01");
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    ASSERT_TRUE(s.open(qpath(files.heavy()))) << s.errorString().toStdString();
    std::atomic<bool> started{false};
    // Рендер 600 dpi (4961 × 7017) — секунды; cookie задачи прерывает его между узлами display list.
    QFuture<int> render = s.post([&](TaskContext& tc) {
        const ot::Renderer renderer(tc.document());
        ot::RenderRequest req;
        req.matrix = ot::Matrix::dpi(600);
        req.scissor = renderer.deviceBounds(0, req.matrix);
        std::vector<std::uint8_t> buf(static_cast<size_t>(req.scissor.width()) * req.scissor.height() * 4);
        started = true;
        renderer.render(req, buf.data(), req.scissor.width() * 4, ot::PixelFormat::Bgra32Premultiplied,
                        tc.cookie());
        return 1;
    });
    ASSERT_TRUE(ot::test::waitUntil([&] { return started.load(); }));
    QThread::msleep(100);  // рендер в разгаре
    ASSERT_FALSE(render.isFinished()) << "страница слишком лёгкая: рендер кончился до отмены";
    QElapsedTimer timer;
    timer.start();
    s.cancelAll();
    EXPECT_EQ(futureErrorKind(render), ErrorKind::Aborted);
    const qint64 ms = timer.elapsed();
    std::printf("cancel latency: %lld ms\n", static_cast<long long>(ms));
    EXPECT_LT(ms, 200);
}

TEST(document_session, edit_bumps_revision_and_signals_pages) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    ASSERT_TRUE(s.open(qpath(files.pdf(3))));
    std::mutex m;
    std::vector<int> pages;
    std::vector<quint64> revisions;
    QObject::connect(
        &s, &DocumentSession::pageChanged, &s,
        [&](int p) {
            const std::lock_guard lock(m);
            pages.push_back(p);
        },
        Qt::DirectConnection);
    QObject::connect(
        &s, &DocumentSession::documentChanged, &s,
        [&](quint64 r) {
            const std::lock_guard lock(m);
            revisions.push_back(r);
        },
        Qt::DirectConnection);
    EXPECT_EQ(s.revision(), 0u);
    QFuture<int> e = s.edit(QStringLiteral("touch"), [](TaskContext& tc) {
        tc.touchPage(2);
        tc.touchPage(0);
        tc.touchPage(2);
        return tc.document().pageCount();
    });
    EXPECT_EQ(e.result(), 3);
    // Чтение не меняет номер правки; неудачная правка — тоже.
    (void)s.postSync([](TaskContext&) { return 0; });
    QFuture<void> bad =
        s.edit(QStringLiteral("bad"), [](TaskContext&) { throw MuError(FZ_ERROR_ARGUMENT, "x"); });
    EXPECT_EQ(futureErrorKind(bad), ErrorKind::Argument);
    s.postSync([](TaskContext&) {});
    EXPECT_EQ(s.revision(), 1u);
    const std::lock_guard lock(m);
    EXPECT_EQ(pages, (std::vector<int>{2, 0}));
    EXPECT_EQ(revisions, (std::vector<quint64>{1}));
}

namespace {

// Правка /Rotate страницы внутри задачи edit (операция журнала уже открыта сессией).
void rotatePage(TaskContext& tc, int page, int rotate) {
    fz_context* ctx = tc.context();
    pdf_document* pdf = pdf_document_from_fz_document(ctx, tc.document().handle());
    ot::mu::call(
        ctx, [&] { pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Rotate), rotate); });
    tc.touchPage(page);
}

int rotationOf(DocumentSession& s, int page) {
    return s.postSync([page](TaskContext& tc) { return tc.document().pageRotation(page); });
}

}  // namespace

TEST(document_session, edit_goes_through_journal_and_undo_redo_restore) {
    OT_FEATURE("edit-13");
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    ASSERT_TRUE(s.open(qpath(files.pdf(3))));
    std::mutex m;
    std::vector<int> pages;
    int undoSignals = 0;
    QObject::connect(
        &s, &DocumentSession::pageChanged, &s,
        [&](int p) {
            const std::lock_guard lock(m);
            pages.push_back(p);
        },
        Qt::DirectConnection);
    QObject::connect(
        &s, &DocumentSession::undoStateChanged, &s,
        [&] {
            const std::lock_guard lock(m);
            ++undoSignals;
        },
        Qt::DirectConnection);
    EXPECT_FALSE(s.undoState().canUndo);
    s.edit(QStringLiteral("Rotate page"), [](TaskContext& tc) { rotatePage(tc, 1, 90); }).waitForFinished();
    EXPECT_EQ(rotationOf(s, 1), 90);
    EXPECT_EQ(s.revision(), 1u);
    ot::UndoState state = s.undoState();
    EXPECT_TRUE(state.canUndo);
    EXPECT_FALSE(state.canRedo);
    EXPECT_EQ(state.undoName, QStringLiteral("Rotate page"));
    {
        const std::lock_guard lock(m);
        pages.clear();
    }
    EXPECT_TRUE(s.undo().result());
    EXPECT_EQ(rotationOf(s, 1), 0);
    EXPECT_EQ(s.revision(), 2u);
    state = s.undoState();
    EXPECT_FALSE(state.canUndo);
    EXPECT_TRUE(state.canRedo);
    EXPECT_EQ(state.redoName, QStringLiteral("Rotate page"));
    EXPECT_FALSE(s.undo().result());  // нечего отменять
    EXPECT_TRUE(s.redo().result());
    EXPECT_EQ(rotationOf(s, 1), 90);
    EXPECT_EQ(s.undoState().undoName, QStringLiteral("Rotate page"));
    const std::lock_guard lock(m);
    EXPECT_EQ(pages, (std::vector<int>{1, 1}));  // страница шага — после undo и после redo
    EXPECT_GE(undoSignals, 3);
}

TEST(document_session, failed_edit_leaves_document_untouched) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    ASSERT_TRUE(s.open(qpath(files.pdf(2))));
    QFuture<void> bad = s.edit(QStringLiteral("Broken"), [](TaskContext& tc) {
        rotatePage(tc, 0, 180);
        throw std::runtime_error("edit failed");
    });
    EXPECT_EQ(futureErrorKind(bad), ErrorKind::Other);
    EXPECT_EQ(rotationOf(s, 0), 0);
    EXPECT_FALSE(s.undoState().canUndo);
    EXPECT_EQ(s.revision(), 0u);
}

TEST(document_session, structural_edit_and_its_undo_refresh_layout) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    ASSERT_TRUE(s.open(qpath(files.pdf(3))));
    s.edit(QStringLiteral("Insert page"), [](TaskContext& tc) {
         fz_context* ctx = tc.context();
         pdf_document* pdf = pdf_document_from_fz_document(ctx, tc.document().handle());
         ot::mu::PdfObjHandle page(ctx);
         ot::mu::call(ctx, [&] {
             page.reset(pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 300, 400), 0, nullptr, nullptr));
             pdf_insert_page(ctx, pdf, 0, page);
         });
         tc.touchAllPages();
     }).waitForFinished();
    EXPECT_EQ(s.pageCount(), 4);
    EXPECT_EQ(s.pageSizePoints(0), QSizeF(300, 400));
    EXPECT_TRUE(s.undo().result());
    EXPECT_EQ(s.pageCount(), 3);
    EXPECT_EQ(s.pageSizePoints(0), QSizeF(595, 842));
    EXPECT_TRUE(s.redo().result());
    EXPECT_EQ(s.pageCount(), 4);
}

TEST(document_session, save_and_save_as_reopen_document_in_same_session) {
    ot::test::qtApp();
    Files files;
    DocumentSession s;
    const fs::path path = files.pdf(2);
    ASSERT_TRUE(s.open(qpath(path)));
    std::mutex m;
    QStringList savedPaths;
    QObject::connect(
        &s, &DocumentSession::saved, &s,
        [&](const QString& p) {
            const std::lock_guard lock(m);
            savedPaths << p;
        },
        Qt::DirectConnection);
    s.edit(QStringLiteral("Rotate page"), [](TaskContext& tc) { rotatePage(tc, 0, 90); }).waitForFinished();
    ot::SaveReport r = s.save().result();
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, ot::SaveMode::Incremental);
    EXPECT_FALSE(s.undoState().canUndo);  // документ переоткрыт — история с нуля
    EXPECT_EQ(rotationOf(s, 0), 90);
    EXPECT_EQ(s.pageCount(), 2);
    EXPECT_TRUE(s.isOpen());
    EXPECT_EQ(ot::Document::openOrThrow(path)->pageRotation(0), 90);
    const fs::path other = path.parent_path() / L"копия сохранения.pdf";
    r = s.saveAs(qpath(other)).result();
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(s.path(), qpath(other));
    // Правки после сохранения идут обычным порядком.
    s.edit(QStringLiteral("Rotate page"), [](TaskContext& tc) { rotatePage(tc, 1, 180); }).waitForFinished();
    EXPECT_TRUE(s.undoState().canUndo);
    const std::lock_guard lock(m);
    EXPECT_EQ(savedPaths, (QStringList{qpath(path), qpath(other)}));
}

TEST(document_session, post_sync_inside_task_runs_inline) {
    ot::test::qtApp();
    DocumentSession s;
    const int v = s.postSync([&s](TaskContext&) { return s.postSync([](TaskContext&) { return 41; }) + 1; });
    EXPECT_EQ(v, 42);
}

TEST(document_session, two_documents_two_threads_do_not_block_each_other) {
    OT_FEATURE("view-27");
    ot::test::qtApp();
    Files files;
    DocumentSession a;
    DocumentSession b;
    ASSERT_TRUE(a.open(qpath(files.pdf(2, L"а"))));
    ASSERT_TRUE(b.open(qpath(files.pdf(3, L"б"))));
    EXPECT_NE(a.id(), b.id());
    EXPECT_NE(a.threadId(), b.threadId());
    std::latch gate(1);
    std::latch started(1);
    QFuture<void> stuck = a.post([&](TaskContext&) {
        started.count_down();
        gate.wait();  // документ «а» занят надолго
    });
    started.wait();
    QElapsedTimer t;
    t.start();
    // «б» работает, пока «а» стоит: открыт свой документ, страница рендерится.
    EXPECT_EQ(b.postSync([](TaskContext& tc) { return tc.document().pageCount(); }), 3);
    EXPECT_FALSE(b.renderPage(2, 36).isNull());
    EXPECT_LT(t.elapsed(), 2000);
    EXPECT_FALSE(stuck.isFinished());
    // Ошибка в «б» не трогает «а».
    QFuture<void> fail = b.post([](TaskContext&) { throw MuError(FZ_ERROR_SYSTEM, "oom"); });
    EXPECT_EQ(futureErrorKind(fail), ErrorKind::System);
    gate.count_down();
    stuck.waitForFinished();
    EXPECT_EQ(a.postSync([](TaskContext& tc) { return tc.document().pageCount(); }), 2);
}

TEST(document_session, destructor_cancels_queue_and_joins) {
    ot::test::qtApp();
    std::vector<QFuture<int>> queued;
    std::latch started(1);
    {
        DocumentSession s;
        (void)s.post([&](TaskContext&) {
            started.count_down();
            QThread::msleep(50);
        });
        for (int i = 0; i < 3; ++i) {
            queued.push_back(s.post([](TaskContext&) { return 1; }));
        }
        started.wait();
    }
    for (auto& f : queued) {
        EXPECT_EQ(futureErrorKind(f), ErrorKind::Aborted);
    }
}

TEST(document_registry, one_tab_per_file_and_isolated_sessions) {
    OT_FEATURE("view-27");
    ot::test::qtApp();
    Files files;
    DocumentRegistry reg;
    std::vector<quint64> openedIds;
    QObject::connect(&reg, &DocumentRegistry::documentOpened, &reg,
                     [&](quint64 id) { openedIds.push_back(id); });
    const auto a = reg.open(qpath(files.pdf(2, L"а")));
    const auto b = reg.open(qpath(files.pdf(4, L"б")));
    ASSERT_NE(a.id, 0u) << a.summary.error.message.toStdString();
    ASSERT_NE(b.id, 0u);
    EXPECT_NE(a.id, b.id);
    EXPECT_FALSE(a.existing);
    EXPECT_EQ(reg.count(), 2);
    // Тот же файл (другой регистр и «\») — та же вкладка.
    QString again = qpath(files.pdf(2, L"а")).toUpper();
    again.replace(QLatin1Char('/'), QLatin1Char('\\'));
    const auto a2 = reg.open(again);
    EXPECT_TRUE(a2.existing);
    EXPECT_EQ(a2.id, a.id);
    EXPECT_EQ(reg.count(), 2);
    EXPECT_EQ(openedIds, (std::vector<quint64>{a.id, b.id}));
    EXPECT_EQ(reg.session(a.id)->pageCount(), 2);
    EXPECT_EQ(reg.session(b.id)->pageCount(), 4);
    EXPECT_NE(reg.session(a.id)->threadId(), reg.session(b.id)->threadId());
    EXPECT_EQ(reg.findByPath(qpath(files.pdf(4, L"б"))), b.id);
    EXPECT_EQ(reg.session(12345), nullptr);
    // Не открылся — вкладки нет.
    const auto bad = reg.open(qpath(files.pdf(1).parent_path() / L"нет.pdf"));
    EXPECT_EQ(bad.id, 0u);
    EXPECT_FALSE(bad.summary.ok);
    EXPECT_EQ(reg.count(), 2);
}

TEST(document_registry, close_waits_for_queued_tasks) {
    ot::test::qtApp();
    Files files;
    DocumentRegistry reg;
    const auto a = reg.open(qpath(files.pdf(2)));
    ASSERT_NE(a.id, 0u);
    auto session = reg.session(a.id);
    std::atomic<int> done{0};
    std::vector<QFuture<void>> tasks;
    for (int i = 0; i < 4; ++i) {
        tasks.push_back(session->post([&](TaskContext& tc) {
            QThread::msleep(20);
            EXPECT_TRUE(tc.hasDocument());  // документ ещё открыт
            ++done;
        }));
    }
    std::vector<quint64> closedIds;
    QObject::connect(&reg, &DocumentRegistry::documentClosed, &reg,
                     [&](quint64 id) { closedIds.push_back(id); });
    EXPECT_TRUE(reg.close(a.id));
    EXPECT_EQ(done.load(), 4);  // задачи до закрытия выполнены, не отменены
    for (auto& f : tasks) {
        EXPECT_EQ(futureErrorKind(f), ErrorKind::None);
    }
    EXPECT_FALSE(session->isOpen());
    EXPECT_EQ(reg.count(), 0);
    EXPECT_EQ(closedIds, (std::vector<quint64>{a.id}));
    EXPECT_FALSE(reg.close(a.id));
}
