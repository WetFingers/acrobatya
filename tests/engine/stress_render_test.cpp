// engine.stress.render — стресс потока документа и рендер-пула (план 1.2, эталон 4.1): 20 потоков-заказчиков
// × 200 страниц корпуса (теги big, cyr, annots, forms) через DocumentSession (display lists из кеша сессии) и
// общий RenderPool; случайные приоритеты, случайные отмены (тайла, всех тайлов документа, всех задач сессии),
// закрытие одного документа и разрушение сессии другого посреди работы. Ожидания: процесс жив (под msvc-asan
// — без ошибок ASan), каждый тайл даёт ровно один результат, одинаковые ключи — одинаковые пиксели, ошибки —
// только отмена, «документ закрыт» и коды битого файла (FORMAT / SYNTAX / LIMIT / UNSUPPORTED). Нет корпуса —
// SKIP.
#include <gtest/gtest.h>

#include <QImage>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/mu/error.h"
#include "core/render/display_cache.h"
#include "core/render/tiles.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/render_pool.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::DocumentSession;
using ot::ErrorKind;
using ot::TileKey;
using ot::TileResult;

namespace {

constexpr int kRequesters = 20;
constexpr size_t kPages = 200;

struct PageRef {
    size_t doc = 0;  // индекс файла
    int page = 0;
};

struct Pick {
    fs::path file;
    int pages = 1;  // сколько первых страниц взять (не больше, чем в файле)
};

// Файлы корпуса: большие — по 30 страниц, кириллица — по 2, аннотации и формы — по первой странице.
std::vector<Pick> pickFiles() {
    std::vector<Pick> files;
    const fs::path indexFile = ot::test::corpusRoot() / L"index.json";
    std::error_code ec;
    if (!fs::exists(indexFile, ec)) {
        return files;
    }
    const json index = json::parse(ot::paths::readFile(indexFile));
    const struct {
        const char* tag;
        size_t files;
        int pagesPerFile;
    } plan[] = {{"big", 4, 30}, {"cyr", 14, 2}, {"annots", 40, 1}, {"forms", 40, 1}};
    for (const auto& step : plan) {
        size_t taken = 0;
        for (const json& rec : index["tags"].value(step.tag, json::array())) {
            if (taken == step.files) {
                break;
            }
            if (rec.value("expect", std::string("open")) != "open") {
                continue;  // пароль, данные, битые — не для стресса рендера
            }
            const fs::path file =
                ot::test::corpusFile(rec.value("path", std::string()), rec.value("rel", std::string()));
            if (!fs::exists(file, ec)) {
                continue;
            }
            files.push_back({file, step.pagesPerFile});
            ++taken;
        }
    }
    return files;
}

std::uint64_t pixelHash(const QImage& img) {
    std::uint64_t h = 1469598103934665603ULL;
    for (int y = 0; y < img.height(); ++y) {
        const uchar* row = img.constScanLine(y);
        for (qsizetype x = 0; x < static_cast<qsizetype>(img.width()) * 4; ++x) {
            h = (h ^ row[x]) * 1099511628211ULL;
        }
    }
    return h;
}

struct Stats {
    std::atomic<long> submitted{0};
    std::atomic<long> results{0};
    std::atomic<long> ok{0};
    std::atomic<long> aborted{0};
    std::atomic<long> broken{0};
    std::atomic<long> unexpected{0};
    std::atomic<long> mismatches{0};
    std::atomic<long> listOk{0};
    std::atomic<long> listAborted{0};
    std::atomic<long> listClosed{0};
    std::atomic<long> listBroken{0};
    std::atomic<long> listUnexpected{0};
    std::atomic<long> cancels{0};
    std::atomic<long> visits{0};
    std::mutex m;
    std::unordered_map<TileKey, std::uint64_t, ot::TileKeyHash> hashes;
    std::vector<std::string> notes;

    std::map<std::string, int> noteCounts;

    // Заметка о неожиданном: одинаковые тексты считаются, не повторяются.
    void note(const std::string& s) {
        const std::lock_guard lock(m);
        if (noteCounts[s]++ == 0 && notes.size() < 40) {
            notes.push_back(s);
        }
    }
};

}  // namespace

TEST(stress, render) {
    OT_FEATURE("view-27");
    const ot::test::QuietLog quiet;
    const std::vector<Pick> files = pickFiles();
    // Сессии: по одной на файл (свой поток и клон контекста); слабые ссылки у заказчиков — сессию можно
    // разрушить. Страницы — первые pick.pages, сколько есть в файле, всего 200.
    std::vector<std::shared_ptr<DocumentSession>> owners(files.size());
    std::vector<std::weak_ptr<DocumentSession>> sessions(files.size());
    std::vector<PageRef> pages;
    for (size_t i = 0; i < files.size() && pages.size() < kPages; ++i) {
        auto s = std::make_shared<DocumentSession>();
        if (!s->open(QString::fromStdWString(files[i].file.wstring()))) {
            continue;
        }
        for (int p = 0; p < std::min(files[i].pages, s->pageCount()) && pages.size() < kPages; ++p) {
            pages.push_back({i, p});
        }
        owners[i] = s;
        sessions[i] = s;
    }
    if (pages.size() < 50) {
        GTEST_SKIP() << "корпуса нет или в нём мало страниц (" << pages.size() << ") — "
                     << ot::paths::toUtf8(ot::test::corpusRoot());
    }
    Stats st;
    {
        ot::RenderPool pool;
        auto onResult = [&st](const TileResult& r) {
            ++st.results;
            if (r.ok()) {
                ++st.ok;
                if (r.image.width() != r.rect.width() || r.image.height() != r.rect.height()) {
                    ++st.unexpected;
                    st.note("tile size mismatch");
                }
                const std::uint64_t h = pixelHash(r.image);
                const std::lock_guard lock(st.m);
                const auto [it, inserted] = st.hashes.emplace(r.key, h);
                if (!inserted && it->second != h) {
                    ++st.mismatches;
                }
            } else if (r.aborted) {
                ++st.aborted;
            } else if (ot::test::isBrokenFileCode(r.error.code)) {
                ++st.broken;
            } else {
                ++st.unexpected;
                st.note("tile error: " + r.error.message.toStdString());
            }
        };
        auto requester = [&](int id) {
            std::mt19937 rng(static_cast<unsigned>(id) * 7919u + 1u);
            std::vector<size_t> order(pages.size());
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            std::shuffle(order.begin(), order.end(), rng);
            const float zooms[] = {0.25f, 0.5f, 0.75f, 1.0f};
            for (size_t n = 0; n < order.size(); ++n) {
                const PageRef ref = pages[order[n]];
                ++st.visits;
                if (id == 0 && n % 50 == 0) {
                    std::printf("progress: %zu/%zu (tiles %ld)\n", n, order.size(), st.submitted.load());
                    std::fflush(stdout);
                }
                const std::shared_ptr<DocumentSession> session = sessions[ref.doc].lock();
                if (!session) {
                    ++st.listClosed;  // сессию уже разрушили
                    continue;
                }
                const auto priority = static_cast<ot::TaskPriority>(rng() % 5);
                QFuture<std::shared_ptr<const ot::DisplayList>> f =
                    session->displayList(ref.page, true, priority);
                if (rng() % 40 == 0) {
                    session->cancelAll();  // чужие задачи этой сессии тоже отменяются
                    ++st.cancels;
                }
                std::shared_ptr<const ot::DisplayList> list;
                try {
                    list = f.result();
                    ++st.listOk;
                } catch (const ot::MuError& e) {
                    if (e.kind() == ErrorKind::Aborted) {
                        ++st.listAborted;
                    } else if (ot::test::isBrokenFileCode(e.code())) {
                        ++st.listBroken;
                    } else {
                        ++st.listUnexpected;
                        st.note(std::string("list MuError: ") + e.what());
                    }
                    continue;
                } catch (const std::logic_error&) {
                    ++st.listClosed;  // документ закрыт
                    continue;
                } catch (const std::exception& e) {
                    ++st.listUnexpected;
                    st.note(std::string("list: ") + e.what());
                    continue;
                }
                const float zoom = zooms[rng() % 4];
                const int rotation = (rng() % 4 == 0) ? 90 : 0;
                const auto tiles = ot::tileRectsFor(list->bounds(), zoom, 1.0f, rotation);
                const ot::Matrix m = ot::pageMatrix(list->bounds(), zoom, 1.0f, rotation);
                std::vector<quint64> jobs;
                for (size_t t = 0; t < tiles.size() && t < 4; ++t) {
                    ot::TileRequest req;
                    req.key = ot::makeTileKey(session->id(), ref.page, zoom, 1.0f, rotation, tiles[t]);
                    req.list = list;
                    req.matrix = m;
                    req.rect = tiles[t].rect;
                    req.priority = static_cast<int>(rng() % 10);
                    ++st.submitted;
                    jobs.push_back(pool.submit(req, nullptr, onResult));
                }
                if (!jobs.empty() && rng() % 8 == 0) {
                    pool.cancel(jobs[rng() % jobs.size()]);
                    ++st.cancels;
                }
                if (rng() % 60 == 0) {
                    pool.cancelDocument(session->id());
                    ++st.cancels;
                }
                if (rng() % 30 == 0) {
                    pool.retarget(session->id(), [&](const TileKey& k) { return k.page == ref.page; }, 20);
                }
            }
        };
        std::vector<std::thread> threads;
        for (int i = 0; i < kRequesters; ++i) {
            threads.emplace_back(requester, i);
        }
        // Посреди работы: закрыть документ первого файла и разрушить сессию второго (их задачи и тайлы в
        // пути).
        while (st.visits.load() < static_cast<long>(kRequesters * pages.size() / 3)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (owners[0]) {
            owners[0]->close();
        }
        if (owners.size() > 1) {
            owners[1].reset();  // последняя сильная ссылка — здесь или у заказчика, что держит её сейчас
        }
        for (auto& t : threads) {
            t.join();
        }
        ASSERT_TRUE(pool.waitForDone(600000));
        EXPECT_EQ(pool.pending(), 0);
    }
    std::printf(
        "stress: pages %zu in %zu files, visits %ld; lists ok %ld, aborted %ld, closed %ld, broken %ld; "
        "tiles %ld: "
        "ok %ld, aborted %ld, broken %ld, unexpected %ld, keys %zu, mismatches %ld; cancels %ld\n",
        pages.size(), files.size(), st.visits.load(), st.listOk.load(), st.listAborted.load(),
        st.listClosed.load(), st.listBroken.load(), st.submitted.load(), st.ok.load(), st.aborted.load(),
        st.broken.load(), st.unexpected.load(), st.hashes.size(), st.mismatches.load(), st.cancels.load());
    for (const auto& n : st.notes) {
        std::printf("  note (%d): %s\n", st.noteCounts[n], n.c_str());
    }
    EXPECT_EQ(st.results.load(), st.submitted.load());  // у каждого тайла ровно один результат
    EXPECT_EQ(st.mismatches.load(), 0);                 // одинаковый ключ — одинаковые пиксели
    EXPECT_EQ(st.unexpected.load(), 0);
    EXPECT_EQ(st.listUnexpected.load(), 0);
    EXPECT_GT(st.ok.load(), 1000);
    EXPECT_GT(st.aborted.load() + st.listAborted.load(), 0);
}
