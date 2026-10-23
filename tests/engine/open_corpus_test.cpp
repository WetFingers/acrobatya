// engine.corpus.* — каждый файл корпуса по тегам (план 1.1): открыть и нарисовать первую страницу 32 × 32.
// Список файлов и ожидания — <корпус>/index.json (tools/fetch_corpus.py; корпус качает только этап 0 —
// нет индекса → SKIP и строка «Нужна ручная проверка»). Ожидания поля "expect": open и sig:… — открылся и
// нарисовался; password:<p> — то же с паролем; broken-ok — процесс жив, ошибка только FORMAT / SYNTAX /
// LIMIT / UNSUPPORTED; data — не PDF, пропуск; "edge": true — граничный случай, неудача только в журнал.
// Тег делится на части по 200 файлов (отдельные тесты); прогресс «progress: n/N» каждые 50 файлов
// (TIMEOUT_AFTER_MATCH "120;^progress:").
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
    int opened = 0, broken = 0, skipped = 0, edgeFailures = 0;
    for (size_t i = shard.first; i < shard.first + shard.count; ++i) {
        const json& rec = records[i];
        const std::string expect = rec.value("expect", std::string("open"));
        const std::string rel = rec.value("rel", rec.value("path", std::string()));
        const bool edge = rec.value("edge", false);
        if (expect == "data") {
            ++skipped;
        } else {
            const std::string password =
                expect.rfind("password:", 0) == 0 ? expect.substr(sizeof("password:") - 1) : std::string();
            const ot::test::ProbeResult p =
                ot::test::probeFile(ot::paths::fromUtf8(rec.value("path", std::string())), password);
            std::string problem;
            if (expect == "broken-ok") {
                if (p.error.kind != ot::ErrorKind::None && !ot::test::isBrokenFileCode(p.error.code)) {
                    problem = std::string("ошибка не из FORMAT/SYNTAX/LIMIT/UNSUPPORTED: ") +
                              ot::MuError::codeName(p.error.code) + " " + p.error.message;
                }
                broken += p.opened ? 0 : 1;
            } else if (!p.opened || !p.rendered) {
                problem = p.needsPassword
                              ? std::string("нужен пароль")
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
        "ошибок %zu\n",
        shard.tag.c_str(), shard.first, shard.first + shard.count, shard.total, opened, broken, skipped,
        edgeFailures, failures.size());
    std::string list;
    for (size_t k = 0; k < failures.size() && k < 20; ++k) {
        list += "\n  " + failures[k];
    }
    EXPECT_TRUE(failures.empty()) << failures.size() << " файлов не прошли:" << list;
}

INSTANTIATE_TEST_SUITE_P(corpus, CorpusOpen, testing::ValuesIn(shards()), shardName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CorpusOpen);
