// engine.content_roundtrip.* — модель контент-потока на корпусе text-edit (план 3.1, п. 3; эталон 3.9
// «Стабильность»): у каждого файла тега (~300: pdfium fx/text и fx/font, pdf.js с текстом, Word, сканы с OCR,
// кириллица) каждая страница (до kMaxPages) разбирается ContentModel, и serialize() без правок даёт каждый
// декодированный поток байт в байт (элементы массива /Contents, inline images, BX/EX — как были); операции
// покрывают поток без пропусков. Попутно — перекрёстная проверка геометрии через pdf_new_run_processor
// (сколько страниц совпали с fz_stext символ в символ, — число в журнал). Файлы, которые не открывает сам
// MuPDF (expect broken-ok, пароль), пропускаются. Нет корпуса — SKIP (корпус качает только этап 0).
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/mu/error.h"
#include "core/util/paths.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::edit::ContentModel;

namespace {

constexpr size_t kShard = 50;
constexpr int kMaxPages = 30;

const json& corpusIndex() {
    static const json index = [] {
        const fs::path p = ot::test::corpusRoot() / L"index.json";
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            return json();
        }
        try {
            return json::parse(ot::paths::readFile(p));
        } catch (const std::exception&) {
            return json();
        }
    }();
    return index;
}

const json& records() {
    static const json none = json::array();
    const json& index = corpusIndex();
    if (!index.is_object() || !index["tags"].contains("text-edit")) {
        return none;
    }
    return index["tags"]["text-edit"];
}

std::vector<size_t> shards() {
    std::vector<size_t> out;
    // Число файлов тега в снимке корпуса — 313; запасные части пропускаются сами, если корпус меньше.
    for (size_t first = 0; first < 350; first += kShard) {
        out.push_back(first);
    }
    return out;
}

class ContentRoundtrip : public testing::TestWithParam<size_t> {};

}  // namespace

TEST_P(ContentRoundtrip, serialize_is_byte_identical) {
    const json& recs = records();
    if (recs.empty()) {
        GTEST_SKIP() << "нет тега text-edit в индексе корпуса — корпус качает этап 0 (tools/fetch_corpus.py)";
    }
    const size_t first = GetParam();
    if (first >= recs.size()) {
        GTEST_SKIP() << "часть за пределами тега (" << recs.size() << " файлов)";
    }
    const size_t last = std::min(recs.size(), first + kShard);
    const ot::test::QuietLog quiet;
    int files = 0, skipped = 0, pages = 0, streams = 0, crossOk = 0, crossPages = 0, forms = 0;
    size_t bytes = 0, ops = 0, glyphs = 0;
    std::vector<std::string> failures;
    for (size_t i = first; i < last; ++i) {
        const json& rec = recs[i];
        const std::string rel = rec.value("rel", rec.value("path", std::string()));
        const std::string expect = rec.value("expect", std::string("open"));
        if (expect != "open") {
            ++skipped;
            continue;
        }
        ot::OpenResult opened =
            ot::Document::open(ot::test::corpusFile(rec.value("path", std::string()), rel));
        if (!opened.ok || !opened.document->isPdf()) {
            ++skipped;
            continue;
        }
        ++files;
        const ot::Document& doc = *opened.document;
        const int n = std::min(doc.pageCount(), kMaxPages);
        for (int page = 0; page < n; ++page) {
            try {
                const ContentModel model = ContentModel::build(doc, page);
                ++pages;
                for (size_t s = 0; s < model.streams().size(); ++s) {
                    const auto& stream = model.streams()[s];
                    forms += stream.form ? 1 : 0;
                    ++streams;
                    bytes += stream.bytes.size();
                    if (model.serialize(static_cast<int>(s)) != stream.bytes) {
                        failures.push_back(rel + " стр. " + std::to_string(page + 1) + " поток " +
                                           std::to_string(s) + ": serialize() не равен исходному");
                    }
                }
                ops += model.operations().size();
                glyphs += model.glyphs().size();
                const ot::edit::CrossCheck check = ContentModel::crossCheck(doc, page, model);
                ++crossPages;
                const bool same = check.mismatches == 0 && check.modelChars == check.stextChars;
                crossOk += same ? 1 : 0;
                if (!same && ot::test::envFlag("OTTISK_CROSS_TRACE")) {
                    std::printf("cross: %s стр. %d: %s (%d расхождений, символов %d / %d)\n", rel.c_str(),
                                page + 1, check.firstMismatch.c_str(), check.mismatches, check.modelChars,
                                check.stextChars);
                }
            } catch (const ot::MuError& e) {
                // Страница, которую не загружает сам MuPDF (битое дерево страниц), — не ошибка модели.
                if (!ot::test::isBrokenFileCode(e.code())) {
                    failures.push_back(rel + " стр. " + std::to_string(page + 1) + ": " + e.what());
                }
            }
        }
    }
    std::printf(
        "content_roundtrip [%zu..%zu): файлов %d, пропущено %d, страниц %d, потоков %d (форм %d), "
        "байт %zu, операций %zu, глифов %zu, совпало с fz_stext страниц %d из %d\n",
        first, last, files, skipped, pages, streams, forms, bytes, ops, glyphs, crossOk, crossPages);
    std::printf("progress: %zu/%zu\n", last - first, last - first);
    for (const auto& f : failures) {
        ADD_FAILURE() << f;
    }
    // Геометрия модели совпадает с fz_stext почти везде: расходятся строки справа налево (fz_stext их
    // переставляет), текст в узорах и Type3-процедурах (модель их не разворачивает) — 862 из 873 страниц
    // снимка.
    EXPECT_GE(crossOk * 10, crossPages * 9) << "совпало с fz_stext " << crossOk << " из " << crossPages;
}

INSTANTIATE_TEST_SUITE_P(text_edit, ContentRoundtrip, testing::ValuesIn(shards()),
                         [](const testing::TestParamInfo<size_t>& info) {
                             return "part_" + std::to_string(info.param / kShard);
                         });
