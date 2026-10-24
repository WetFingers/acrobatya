// engine.sig.dss — регрессия валидатора на корпусе EU DSS (план 4.4, п. 5; эталон 3.8 «Acrobat», 9.4;
// sign-25): все файлы тегов sig-b/t/lt/lta корпуса dss (index.json), режим сверки с pyHanko — якоря —
// самоподписанные сертификаты самого файла, отзыв retroactive без сети («по возможности»), время — метка
// подписи, иначе /M. Вердикт подписи сводится к {kind, intact, trusted, modified, level} (как
// tools/compare_pyhanko.py) и сравнивается с tests/fixtures/sig/dss_expectations.json: файл совпал, если
// совпали все его подписи; доля совпадений ≥ 90 %. Наши вердикты — build/<пресет>/dss/ours.json
// (compat.sig.pyhanko — сверка с pyHanko и отчёт расхождений). Нет корпуса — SKIP.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/probe.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::sign::ChangeLevel;
using ot::sign::DocumentVerification;
using ot::sign::Issue;
using ot::sign::SignatureInfo;
using ot::sign::SignatureKind;

namespace {

const char* const kTags[] = {"sig-b", "sig-t", "sig-lt", "sig-lta"};

// Изменения после подписи — как modified_of в compare_pyhanko.py.
std::string modifiedOf(const SignatureInfo& s) {
    if (!s.structureOk) {
        return "illegal";
    }
    if (s.coversWholeFile) {
        return "none";
    }
    if (s.has(Issue::ModifiedAfterSigning)) {
        return "unknown";
    }
    if (!s.changes.allowed) {
        return "illegal";
    }
    switch (s.changes.level) {
        case ChangeLevel::None:
            return "none";
        case ChangeLevel::LtvOnly:
            return "ltv";
        case ChangeLevel::FormFilling:
            return "form";
        case ChangeLevel::Annotations:
            return "annot";
        case ChangeLevel::Other:
            return "illegal";
    }
    return "illegal";
}

json record(const SignatureInfo& s) {
    const bool dts = s.kind == SignatureKind::DocTimestamp;
    json r = {{"field", s.field},
              {"kind", dts ? "dts" : "sig"},
              {"intact", s.intact},
              {"trusted", s.trusted},
              {"modified", modifiedOf(s)},
              {"level", dts ? std::string("DTS") : std::string(ot::sign::padesLevelName(s.level))},
              {"verdict", ot::sign::verdictName(s.verdict)}};
    json issues = json::array();
    for (const ot::sign::Finding& f : s.findings) {
        issues.push_back(std::string(ot::sign::issueName(f.issue)) + ": " + f.detail.substr(0, 300));
    }
    r["issues"] = issues;
    json changes = json::array();
    for (const ot::sign::Change& c : s.changes.changes) {
        changes.push_back(std::string(c.allowed ? "+" : "-") + ot::sign::changeKindName(c.kind) + " #" +
                          std::to_string(c.object) + " " + c.detail);
    }
    r["changes"] = changes;
    return r;
}

std::string coarse(const std::string& m) { return m == "ltv" || m == "form" || m == "annot" ? "allowed" : m; }

// Расхождения файла с ожиданием (как file_diffs в compare_pyhanko.py).
std::vector<std::string> diffs(const json& want, const json& got) {
    std::vector<std::string> out;
    if (want.size() != got.size()) {
        out.push_back("signatures: want " + std::to_string(want.size()) + ", got " +
                      std::to_string(got.size()));
    }
    for (const json& w : want) {
        const json* g = nullptr;
        for (const json& x : got) {
            if (x["field"] == w["field"]) {
                g = &x;
                break;
            }
        }
        const std::string field = w.value("field", std::string());
        if (g == nullptr) {
            out.push_back(field + ": missing");
            continue;
        }
        if (w.value("error", false)) {
            continue;
        }
        for (const char* key : {"kind", "intact", "trusted", "level"}) {
            if (w.contains(key) && w[key] != (*g)[key]) {
                out.push_back(field + ": " + key + " want " + w[key].dump() + ", got " + (*g)[key].dump());
            }
        }
        if (w.contains("modified") &&
            coarse(w["modified"].get<std::string>()) != coarse((*g)["modified"].get<std::string>())) {
            out.push_back(field + ": modified want " + w["modified"].dump() + ", got " +
                          (*g)["modified"].dump());
        }
    }
    return out;
}

}  // namespace

TEST(sig, dss) {
    OT_FEATURE("sign-25");
    const fs::path indexPath = ot::test::corpusRoot() / L"index.json";
    std::error_code ec;
    if (!fs::exists(indexPath, ec)) {
        GTEST_SKIP() << "нет корпуса (index.json)";
    }
    const json index = json::parse(ot::paths::readFile(indexPath));
    std::vector<std::pair<std::string, fs::path>> files;
    std::set<std::string> seen;
    for (const char* tag : kTags) {
        for (const json& rec : index["tags"].value(tag, json::array())) {
            if (rec.value("entry", std::string()).rfind("dss", 0) != 0) {
                continue;
            }
            const std::string rel = rec["rel"].get<std::string>();
            const std::size_t at = rel.find("/validation/");
            const std::string name = at == std::string::npos ? rel : rel.substr(at + 12);
            if (seen.insert(name).second) {
                files.emplace_back(name, ot::test::corpusFile(rec["path"].get<std::string>(), rel));
            }
        }
    }
    if (files.empty()) {
        GTEST_SKIP() << "нет файлов dss в тегах sig-*";
    }
    const json expect = json::parse(ot::paths::readFile(ot::test::sourceDir() / L"tests" / L"fixtures" /
                                                        L"sig" / L"dss_expectations.json"))["files"];

    json ours = json::object();
    int total = 0;
    int matched = 0;
    std::vector<std::string> report;
    ot::test::QuietLog quiet;
    for (const auto& [name, path] : files) {
        json sigs = json::array();
        try {
            auto doc = Document::openOrThrow(path);
            const DocumentVerification plain = ot::sign::verifyDocument(*doc);
            const ot::sign::TrustStore roots = ot::test::embeddedRoots(plain);
            ot::sign::VerifyOptions options;
            options.trust = &roots;
            options.retroactiveRevocation = true;
            const DocumentVerification v = ot::sign::verifyDocument(*doc, options);
            for (const SignatureInfo& s : v.signatures) {
                sigs.push_back(record(s));
            }
        } catch (const std::exception& e) {
            ours[name]["error"] = e.what();
        }
        ours[name]["signatures"] = sigs;
        ++total;
        if (!expect.contains(name)) {
            report.push_back(name + ": no expectation");
            continue;
        }
        const std::vector<std::string> d = diffs(expect[name]["signatures"], sigs);
        if (d.empty()) {
            ++matched;
        } else {
            std::string line = name + ":";
            for (const std::string& x : d) {
                line += " " + x + ";";
            }
            report.push_back(line);
        }
    }
    const fs::path outDir = fs::path(OTTISK_BINARY_DIR) / L"dss";
    fs::create_directories(outDir, ec);
    ot::paths::writeFile(outDir / L"ours.json", json({{"files", ours}}).dump(1));
    const double share = total == 0 ? 0.0 : static_cast<double>(matched) / total;
    std::cout << "dss: " << matched << " of " << total << " files match the expectations ("
              << static_cast<int>(share * 1000) / 10.0 << " %)\n";
    for (const std::string& line : report) {
        std::cout << "  " << line << "\n";
    }
    EXPECT_GE(share, 0.9) << "доля совпадений с dss_expectations.json";
}
