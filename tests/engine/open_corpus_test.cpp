// engine.corpus.* — каждый файл корпуса по тегам (план 1.1): открыть и нарисовать первую страницу 32 × 32.
// Список файлов и ожидания — <корпус>/index.json (tools/fetch_corpus.py; корпус качает только этап 0 —
// нет индекса → SKIP и строка «Нужна ручная проверка»). Ожидания поля "expect": open и sig:… — открылся и
// нарисовался; password:<p> — то же с паролем; broken-ok — процесс жив, ошибка битого файла
// (isBrokenFileCode), ARGUMENT из самого MuPDF (ремонт битых объектов: «not a dict»), нехватка памяти под
// пределом engine_tests или рендер дольше срока зонда (probe.h); ввод-вывод и исключения нашего кода —
// провал; data — не PDF, пропуск; "edge": true — граничный случай, неудача только в журнал. Тег делится на
// части по 200 файлов (отдельные тесты); прогресс «progress: n/N» каждые 50 файлов (TIMEOUT_AFTER_MATCH
// "120;^progress:"). OTTISK_CORPUS_TRACE=1 — печатать каждый файл перед зондом (найти файл, на котором упал
// процесс). Файлы из tests/corpus/slow.json (сам MuPDF, `mutool draw`, рисует их минутами внутри одной
// операции — cookie не прерывает) пропускаются.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* const kTags[] = {"annots", "forms", "enc-r2",    "enc-r3",    "enc-r4", "enc-r5",
                             "enc-r6", "big",   "pdfa-pass", "pdfa-fail", "cyr",    "fuzz"};
constexpr size_t kShard = 200;

struct Shard {
    std::string tag;
    size_t first = 0;
    size_t count = 0;
    size_t total = 0;
};

void PrintTo(const Shard& s, std::ostream* os) { *os << s.tag << " " << s.first << "+" << s.count; }

fs::path indexPath() { return ot::test::corpusRoot() / L"index.json"; }

const json& corpusIndex() {
    static const json index = [] {
        std::error_code ec;
        if (!fs::exists(indexPath(), ec)) {
            return json();
        }
        try {
            return json::parse(ot::paths::readFile(indexPath()));
        } catch (const std::exception&) {
            return json();
        }
    }();
    return index;
}

std::vector<Shard> shards() {
    std::vector<Shard> out;
    const json& index = corpusIndex();
    if (!index.is_object()) {
        return out;
    }
    for (const char* tag : kTags) {
        const size_t n = index["tags"].value(tag, json::array()).size();
        for (size_t first = 0; first < n; first += kShard) {
            out.push_back({tag, first, std::min(kShard, n - first), n});
        }
    }
    return out;
}

std::string shardName(const testing::TestParamInfo<Shard>& info) {
    std::string n = info.param.tag + "_" + std::to_string(info.param.first / kShard);
    for (char& c : n) {
        if (c == '-') {
            c = '_';
        }
    }
    return n;
}

// tests/corpus/slow.json → {"files": {rel: причина}}; нет файла — пусто.
const json& slowFiles() {
    static const json files = [] {
        const fs::path p = ot::paths::fromUtf8(OTTISK_SOURCE_DIR) / L"tests" / L"corpus" / L"slow.json";
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            return json::object();
        }
        try {
            return json::parse(ot::paths::readFile(p)).value("files", json::object());
        } catch (const std::exception&) {
            return json::object();
        }
    }();
    return files;
}

class CorpusOpen : public testing::TestWithParam<Shard> {};

}  // namespace

TEST(corpus, open) {
    if (!corpusIndex().is_object()) {
        GTEST_SKIP() << "нет индекса корпуса " << ot::paths::toUtf8(indexPath())
                     << " — корпус качает этап 0 (tools/fetch_corpus.py), сессии не качают";
    }
    std::string missing;
    for (const char* tag : kTags) {
        if (corpusIndex()["tags"].value(tag, json::array()).empty()) {
            missing += std::string(missing.empty() ? "" : ", ") + tag;
        }
    }
    if (!missing.empty()) {
        GTEST_SKIP() << "в индексе корпуса нет тегов: " << missing
                     << " (остальные теги проверены своими тестами)";
    }
}

TEST_P(CorpusOpen, files) {
    OT_FEATURE("view-01");
    const Shard& shard = GetParam();
    const json& records = corpusIndex()["tags"][shard.tag];
    const ot::test::QuietLog quiet;
    std::vector<std::string> failures;
    int opened = 0, broken = 0, skipped = 0, edgeFailures = 0, argumentErrors = 0, memoryFailures = 0,
        slowRenders = 0, slowSkipped = 0;
    const bool trace = ot::test::envFlag("OTTISK_CORPUS_TRACE");
    for (size_t i = shard.first; i < shard.first + shard.count; ++i) {
        const json& rec = records[i];
        const std::string expect = rec.value("expect", std::string("open"));
        const std::string rel = rec.value("rel", rec.value("path", std::string()));
        const bool edge = rec.value("edge", false);
        if (expect == "data") {
            ++skipped;
        } else if (slowFiles().contains(rel)) {
            ++slowSkipped;
        } else {
            const std::string password =
                expect.rfind("password:", 0) == 0 ? expect.substr(sizeof("password:") - 1) : std::string();
            if (trace) {
                std::fprintf(stderr, "trace: %zu %s\n", i, rel.c_str());
                std::fflush(stderr);
            }
            const ot::test::ProbeResult p =
                ot::test::probeFile(ot::test::corpusFile(rec.value("path", std::string()), rel), password);
            std::string problem;
            if (expect == "broken-ok") {
                if (p.error.kind == ot::ErrorKind::None || ot::test::isBrokenFileCode(p.error.code)) {
                    // ошибки нет или это ошибка битого файла
                } else if (p.timedOut) {
                    ++slowRenders;
                } else if (p.error.kind == ot::ErrorKind::Argument) {
                    ++argumentErrors;
                } else if (ot::test::isMemoryFailure(p.error)) {
                    ++memoryFailures;
                } else {
                    problem = std::string("ошибка не битого файла: ") + ot::MuError::codeName(p.error.code) +
                              " " + p.error.message;
                }
                broken += p.opened ? 0 : 1;
            } else if (!p.opened || !p.rendered) {
                problem = p.needsPassword ? std::string("нужен пароль")
                          : p.timedOut
                              ? std::string("рендер дольше срока зонда")
                              : std::string(ot::MuError::codeName(p.error.code)) + " " + p.error.message;
            }
            opened += p.opened ? 1 : 0;
            if (!problem.empty()) {
                if (edge) {
                    ++edgeFailures;
                } else {
                    failures.push_back(rel + " (" + expect + "): " + problem);
                }
            }
        }
        const size_t done = i - shard.first + 1;
        if (done % 50 == 0 || done == shard.count) {
            std::printf("progress: %zu/%zu\n", done, shard.count);
            std::fflush(stdout);
        }
    }
    std::printf(
        "corpus %s [%zu..%zu) из %zu: открыто %d, битых без открытия %d, пропущено %d, edge-неудач %d, "
        "ARGUMENT %d, нехватка памяти %d, долгий рендер %d, медленных в MuPDF %d, ошибок %zu\n",
        shard.tag.c_str(), shard.first, shard.first + shard.count, shard.total, opened, broken, skipped,
        edgeFailures, argumentErrors, memoryFailures, slowRenders, slowSkipped, failures.size());
    std::string list;
    for (size_t k = 0; k < failures.size() && k < 20; ++k) {
        list += "\n  " + failures[k];
    }
    EXPECT_TRUE(failures.empty()) << failures.size() << " файлов не прошли:" << list;
}

INSTANTIATE_TEST_SUITE_P(corpus, CorpusOpen, testing::ValuesIn(shards()), shardName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CorpusOpen);
