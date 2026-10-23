// bench ui-paging — листание в виде (план 2.2b; эталон 5.8 «Смена страницы при 100 % из кеша / без кеша ≤ 50
// / ≤ 150 мс», «PDF32000_2008.pdf: первый растр ≤ 500 мс»): PageView offscreen 1440 × 900, непрерывный режим,
// 100 % (96 dpi), свои RenderPool и TileCache на 1 ГБ (из кеша — значит, все тайлы прохода в кеше).
// Прогон — QTest-набор (строка данных на прогон):
//   first_frame_ms   открыть DocumentSession → setDocument → первый кадр, где все видимые тайлы готовы
//   page_cold_ms     goToPage(p), p = 1…pages: до полного кадра, кеш тайлов и списков пуст (QBENCHMARK_ONCE
//                    — весь проход), в JSON — медиана на страницу
//   page_warm_ms     тот же проход второй раз: тайлы из кеша — медиана на страницу
// Полный кадр — TilePainter::visibleComplete() после отрисовки. QTest зовёт функцию замера ещё и разогревом
// (walltime): итог строки — последний вызов. Числа Debug с целями не сравнимы.
#include "bench_ui.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/mu/context.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/render_pool.h"
#include "services/tile_cache.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace {

constexpr int kFrameTimeoutMs = 60000;
constexpr qint64 kCacheMegabytes = 1024;

double median(std::vector<double> v) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Кадр с готовыми видимыми тайлами: одна отрисовка сразу (флаг прошлого кадра устарел), дальше — события
// (тайл пришёл → update → отрисовка).
bool waitFullFrame(ot::ui::PageView& view) {
    view.viewport()->repaint();
    QElapsedTimer t;
    t.start();
    while (!view.tilePainter().visibleComplete()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (t.elapsed() > kFrameTimeoutMs) {
            return false;
        }
        QThread::yieldCurrentThread();
    }
    return true;
}

class UiPagingBench : public QObject {
    Q_OBJECT
public:
    UiPagingBench(QString file, int runs, int pages) : file_(std::move(file)), runs_(runs), pages_(pages) {}

    struct Run {
        double firstFrame = 0;
        double cold = 0;
        double warm = 0;
    };
    std::map<int, Run> runs;  // прогон → итог последнего вызова (разогрев QTest перезаписывается)
    int pageCount = 0;

private slots:
    void paging_data() {
        QTest::addColumn<int>("run");
        for (int r = 0; r < runs_; ++r) {
            QTest::newRow(qPrintable(QStringLiteral("run %1").arg(r + 1))) << r;
        }
    }

    void paging() {
        QFETCH(int, run);
        ot::MuContext::forThisThread().emptyStore();
        ot::RenderPool pool;
        ot::TileCache cache(ot::TileCache::limitFromMegabytes(kCacheMegabytes));
        ot::DocumentSession session;
        ot::ui::PageView view;
        view.resize(1440, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        QElapsedTimer t;
        t.start();
        QVERIFY(session.open(file_));
        view.setDocument(&session, &pool, &cache);
        view.setZoomPercent(100);
        QVERIFY(waitFullFrame(view));
        const double firstFrame = static_cast<double>(t.nsecsElapsed()) / 1e6;
        pageCount = view.pageCount();
        const int last = std::min(pages_, view.pageCount());

        std::vector<double> cold;
        QBENCHMARK_ONCE {
            for (int p = 1; p < last; ++p) {
                t.restart();
                view.goToPage(p);
                QVERIFY(waitFullFrame(view));
                cold.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
            }
        }
        std::vector<double> warm;
        view.goToPage(0);
        QVERIFY(waitFullFrame(view));
        for (int p = 1; p < last; ++p) {
            t.restart();
            view.goToPage(p);
            QVERIFY(waitFullFrame(view));
            warm.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
        }
        runs[run] = Run{firstFrame, median(cold), median(warm)};
        std::printf("ui-paging run %d: first %.1f ms, cold %.1f ms/page, warm %.1f ms/page (%d pages)\n",
                    run + 1, firstFrame, median(cold), median(warm), last);
        view.setDocument(nullptr);
        session.close();
    }

private:
    QString file_;
    int runs_;
    int pages_;
};

}  // namespace

namespace ot::bench {

int runUiPaging(const std::filesystem::path& file, int runs, int pages,
                const std::filesystem::path& jsonOut) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");  // без экрана, как ctest
    }
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    }
    int argc = 1;
    char arg0[] = "bench";
    char* argv[] = {arg0, nullptr};
    QApplication app(argc, argv);
    ot::ui::Theme::apply(app, ot::ui::ThemeChoice::Light);

    UiPagingBench bench(QString::fromStdWString(file.wstring()), runs, pages);
    const int failed = QTest::qExec(&bench, QStringList{QStringLiteral("bench")});
    std::map<std::string, std::vector<double>> samples;
    for (const auto& [run, r] : bench.runs) {
        samples["first_frame_ms"].push_back(r.firstFrame);
        samples["page_cold_ms"].push_back(r.cold);
        samples["page_warm_ms"].push_back(r.warm);
    }
    if (failed != 0 || samples.empty()) {
        std::fprintf(stderr, "bench ui-paging: %d failed\n", failed);
        return 1;
    }
    nlohmann::json benches = nlohmann::json::array();
    for (const auto& [metric, values] : samples) {
        benches.push_back({{"name", "ui-paging/" + metric}, {"unit", "ms"}, {"samples", values}});
        std::printf("%-12s %-20s median %9.2f ms (n=%zu)\n", "ui-paging", metric.c_str(), median(values),
                    values.size());
    }
    const nlohmann::json j = {{"tool", "bench"},
                              {"name", "ui-paging"},
                              {"file", ot::paths::toUtf8(file.filename())},
                              {"pages", bench.pageCount},
                              {"paged", std::min(pages, bench.pageCount)},
                              {"runs", runs},
                              {"viewport", "1440x900"},
                              {"zoom_percent", 100},
                              {"mupdf", ot::MuContext::version()},
#ifdef NDEBUG
                              {"build", "release"},
#else
                              {"build", "debug"},
#endif
                              {"benchmarks", benches}};
    if (!jsonOut.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(jsonOut.parent_path(), ec);
        ot::paths::writeFile(jsonOut, j.dump(1) + "\n");
    }
    return 0;
}

}  // namespace ot::bench

#include "bench_ui.moc"
