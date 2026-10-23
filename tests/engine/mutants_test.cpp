// engine.mutants.* — 100 своих битых PDF (tools/gen/make_mutants.py, seed 1: усечение, startxref, /Length,
// петли /Kids и /Prev, flate-бомбы, битые байты, /Size, /Count, вложенность, xref). Ожидание (план 1.1):
// процесс жив, файл открывается (с ремонтом) или ошибка MuPDF с кодом FORMAT / SYNTAX / LIMIT / UNSUPPORTED;
// под ASan (пресет msvc-asan) — чисто. Мутанты делает ctest-фикстура engine.mutants.generate в
// <build>/gen/mutants; если её нет — берутся <корпус>/gen/mutants (этап 0).
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path mutantsDir() {
    const fs::path built = ot::paths::fromUtf8(OTTISK_BINARY_DIR) / L"gen" / L"mutants";
    std::error_code ec;
    if (fs::exists(built / L"index.json", ec)) {
        return built;
    }
    return ot::test::corpusRoot() / L"gen" / L"mutants";
}

}  // namespace

TEST(mutants, open_and_render_without_crash) {
    OT_FEATURE("view-01");
    const fs::path dir = mutantsDir();
    std::error_code ec;
    if (!fs::exists(dir / L"index.json", ec)) {
        GTEST_SKIP() << "нет мутантов: ни " << OTTISK_BINARY_DIR
                     << "/gen/mutants (фикстура engine.mutants.generate), "
                     << "ни " << ot::paths::toUtf8(dir);
    }
    const json index = json::parse(ot::paths::readFile(dir / L"index.json"));
    const json& list = index["mutants"];
    ASSERT_EQ(list.size(), 100u) << "make_mutants.py: ровно 100 мутантов";
    ASSERT_EQ(index.value("seed", 0), 1);
    const ot::test::QuietLog quiet;
    std::map<std::string, int> outcomes;
    double slowest = 0;
    std::string slowestName;
    int n = 0;
    for (const json& m : list) {
        const std::string name = m["file"].get<std::string>();
        const ot::test::ProbeResult p = ot::test::probeFile(dir / ot::paths::fromUtf8(name));
        ++n;
        if (p.error.code != 0) {
            EXPECT_TRUE(ot::test::isBrokenFileCode(p.error.code))
                << name << " (" << m["kind"].get<std::string>() << "): код "
                << ot::MuError::codeName(p.error.code) << " — " << p.error.message;
            ++outcomes[std::string("error:") + ot::MuError::codeName(p.error.code)];
        } else if (p.error.kind != ot::ErrorKind::None) {
            ADD_FAILURE() << name << ": не ошибка MuPDF — " << p.error.message;
        } else if (p.needsPassword) {
            ++outcomes["password"];
        } else {
            ++outcomes[p.repaired ? "opened:repaired" : "opened"];
        }
        if (p.ms > slowest) {
            slowest = p.ms;
            slowestName = name;
        }
        if (n % 50 == 0) {
            std::printf("progress: %d/%zu\n", n, list.size());
            std::fflush(stdout);
        }
    }
    std::string summary;
    for (const auto& [k, v] : outcomes) {
        summary += k + "=" + std::to_string(v) + " ";
    }
    std::printf("mutants: %s; дольше всех %s (%.0f мс)\n", summary.c_str(), slowestName.c_str(), slowest);
    RecordProperty("outcomes", summary);
}
