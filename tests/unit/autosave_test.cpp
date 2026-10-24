// unit.autosave.* — автосохранение и восстановление (эталон 4.5, view-27): журнал + копия исходника +
// index.json; восстановление 3 правок после «падения» (taskkill /F дочернего процесса посреди правок),
// отменённые шаги остаются «Вернуть», изменённый исходник — из копии, без копии — отказ (sha256 не совпал),
// нет несохранённого — запись удаляется, восстановленный файл (ремонт) тоже восстанавливается.
// unit.autosave_runner.* — таймер, интервал 1–30, «Сохранить»/«Не сохранять» удаляют запись, восстановление в
// сессии.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFuture>

#include <windows.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "core/doc/autosave.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/features.h"
#include "core/util/hash.h"
#include "core/util/paths.h"
#include "services/autosave_runner.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/qt_app.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Autosave;
using ot::AutosaveEntry;
using ot::AutosaveStatus;
using ot::Document;
using ot::Journal;
using ot::RecoverError;

namespace {

fs::path put(const fs::path& dir, const std::wstring& name, const std::string& bytes) {
    const fs::path p = dir / name;
    ot::paths::writeFile(p, bytes);
    return p;
}

void rotate(Document& doc, int page, int degrees) {
    Journal::Operation op(doc, "Rotate page");
    ot::test::setRotate(doc, page, degrees);
    op.invalidate(page);
    op.commit();
}

void pageMode(Document& doc) {
    Journal::Operation op(doc, "Page mode");
    ot::test::putRootName(doc, "PageMode", "UseOutlines");
    op.commit();
}

// Три правки разного рода: /Rotate, каталог, записка.
void threeEdits(Document& doc) {
    rotate(doc, 0, 90);
    pageMode(doc);
    ot::test::addNote(doc, 1, "восстановлено");
}

int noteCount(Document& doc, int page) {
    fz_context* ctx = doc.context().get();
    int n = 0;
    ot::mu::call(ctx, [&] {
        n = pdf_array_len(ctx,
                          pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, doc.pdf(), page), PDF_NAME(Annots)));
    });
    return n;
}

bool hasUnsaved(Document& doc) {
    fz_context* ctx = doc.context().get();
    int has = 0;
    ot::mu::call(ctx, [&] { has = pdf_has_unsaved_changes(ctx, doc.pdf()); });
    return has != 0;
}

void expectThreeEdits(Document& doc) {
    EXPECT_EQ(doc.pageRotation(0), 90);
    EXPECT_EQ(ot::test::rootName(doc, "PageMode"), "UseOutlines");
    EXPECT_GE(noteCount(doc, 1), 1);
}

// Дочерний процесс для «падения»: unit_tests --gtest_also_run_disabled_tests --gtest_filter=autosave_child.*
// с OTTISK_AUTOSAVE_CHILD_DIR (каталог автосохранения) и OTTISK_AUTOSAVE_CHILD_FILE (документ).
PROCESS_INFORMATION startChild(const fs::path& autosaveDir, const fs::path& file) {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size())));
    std::wstring cmd = L"\"" + exe + L"\" --gtest_also_run_disabled_tests --gtest_filter=autosave_child.*";
    SetEnvironmentVariableW(L"OTTISK_AUTOSAVE_CHILD_DIR", autosaveDir.c_str());
    SetEnvironmentVariableW(L"OTTISK_AUTOSAVE_CHILD_FILE", file.c_str());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   nullptr, &si, &pi);
    SetEnvironmentVariableW(L"OTTISK_AUTOSAVE_CHILD_DIR", nullptr);
    SetEnvironmentVariableW(L"OTTISK_AUTOSAVE_CHILD_FILE", nullptr);
    EXPECT_NE(ok, 0);
    return pi;
}

int runAndWait(std::wstring cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                       &pi) == 0) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

QString qpath(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

}  // namespace

TEST(autosave, three_edits_are_recovered_with_history_after_crash) {
    OT_FEATURE("view-27");
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"отчёт.pdf", ot::test::makePdf(ot::MuContext::global(), 3));
    const std::string original = ot::paths::readFile(file);
    Autosave store(dir.path() / L"autosave");
    {
        auto doc = Document::openOrThrow(file);
        EXPECT_EQ(store.save(*doc), AutosaveStatus::NothingToSave);  // правок нет — нечего писать
        threeEdits(*doc);
        ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
        // Документ в памяти не изменился: история та же, правки на месте.
        EXPECT_EQ(doc->undoStack().position(), 3);
        expectThreeEdits(*doc);
        EXPECT_TRUE(doc->undoStack().canUndo());
        (void)doc->undoStack().undo();
        EXPECT_EQ(ot::test::rootName(*doc, "PageMode"), "UseOutlines");
        (void)doc->undoStack().redo();
        // «Падение»: документ закрыт без сохранения.
    }
    EXPECT_EQ(ot::paths::readFile(file), original);
    const auto entry = store.find(file);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->path, file);
    EXPECT_EQ(entry->sha256, ot::hash::sha256Hex(original));
    EXPECT_EQ(entry->position, 3);
    EXPECT_EQ(entry->steps, 3);
    EXPECT_TRUE(entry->hasCopy);
    EXPECT_TRUE(fs::exists(store.journalPath(entry->key)));
    EXPECT_TRUE(fs::exists(store.copyPath(entry->key)));
    ASSERT_EQ(store.entries().size(), 1u);

    ot::RecoverResult r = store.recover(*entry);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_FALSE(r.fromCopy);
    Document& doc = *r.document;
    EXPECT_EQ(doc.path(), file);
    expectThreeEdits(doc);
    EXPECT_TRUE(hasUnsaved(doc));
    EXPECT_EQ(doc.undoStack().size(), 3);
    EXPECT_EQ(doc.undoStack().undoName(), "Add note");
    (void)doc.undoStack().undo();
    EXPECT_EQ(noteCount(doc, 1), 0);
    (void)doc.undoStack().redo();
    // Сохранить восстановленное — правки в файле; запись удаляется.
    const ot::SaveReport saved = ot::Saver::save(doc);
    ASSERT_TRUE(saved.ok()) << saved.message;
    store.remove(file);
    EXPECT_FALSE(store.find(file).has_value());
    EXPECT_FALSE(fs::exists(store.journalPath(entry->key)));
    EXPECT_FALSE(fs::exists(store.copyPath(entry->key)));
    auto fresh = Document::openOrThrow(file);
    expectThreeEdits(*fresh);
}

// Свойства документа (/Info и поток XMP — Document::setProperties) переживают запись журнала и восстановление
// на настоящем файле с XMP и сжатыми потоками (tests/fixtures/demo/report.pdf; найдено в 2.4b сценарием
// doc.setProperty → doc.saveCopy).
TEST(autosave, properties_edit_with_xmp_stream_is_recovered) {
    ot::test::TempDir dir;
    const fs::path src = put(dir.path(), L"report.pdf",
                             ot::paths::readFile(ot::test::sourceDir() / L"tests/fixtures/demo/report.pdf"));
    std::string before;
    {
        auto doc = Document::openOrThrow(src);
        before = doc->properties().title;
        ot::DocProperties p = doc->properties();
        p.title = "Заголовок из автосохранения";
        doc->setProperties(p);
        Autosave autosave(dir.path() / L"autosave");
        std::string error;
        ASSERT_EQ(autosave.save(*doc, &error), AutosaveStatus::Saved) << error;
        EXPECT_EQ(doc->properties().title, "Заголовок из автосохранения") << "document unchanged by autosave";
        const std::optional<AutosaveEntry> entry = autosave.find(src);
        ASSERT_TRUE(entry.has_value());
        ot::RecoverResult r = autosave.recover(*entry);
        ASSERT_TRUE(r.ok()) << r.message;
        EXPECT_EQ(r.document->properties().title, "Заголовок из автосохранения");
        ASSERT_TRUE(r.document->undoStack().canUndo());
        (void)r.document->undoStack().undo();
        EXPECT_EQ(r.document->properties().title, before);
    }
}

TEST(autosave, undone_steps_stay_redoable_after_recovery) {
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"redo.pdf", ot::test::makePdf(ot::MuContext::global(), 3));
    Autosave store(dir.path() / L"autosave");
    {
        auto doc = Document::openOrThrow(file);
        doc->undoStack().setGroupWindow(std::chrono::milliseconds(0));
        threeEdits(*doc);
        (void)doc->undoStack().undo();  // записка отменена — остаётся «Вернуть»
        ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
        EXPECT_EQ(doc->undoStack().position(), 2);
        EXPECT_EQ(doc->undoStack().redoName(), "Add note");
    }
    const auto entry = store.find(file);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->position, 2);
    EXPECT_EQ(entry->steps, 3);
    ot::RecoverResult r = store.recover(*entry);
    ASSERT_TRUE(r.ok()) << r.message;
    Document& doc = *r.document;
    EXPECT_EQ(doc.pageRotation(0), 90);
    EXPECT_EQ(noteCount(doc, 1), 0);
    ASSERT_TRUE(doc.undoStack().canRedo());
    EXPECT_EQ(doc.undoStack().redoName(), "Add note");
    (void)doc.undoStack().redo();
    EXPECT_GE(noteCount(doc, 1), 1);  // записка и её Popup
}

TEST(autosave, changed_source_recovers_from_copy_and_without_copy_is_refused) {
    ot::test::TempDir dir;
    const std::string bytes = ot::test::makePdf(ot::MuContext::global(), 3);
    const std::string other = ot::test::makePdf(ot::MuContext::global(), 4);
    for (const bool withCopy : {true, false}) {
        SCOPED_TRACE(withCopy ? "copy" : "no copy");
        const fs::path file = put(dir.path(), withCopy ? L"с копией.pdf" : L"без копии.pdf", bytes);
        Autosave store(dir.path() / L"autosave", withCopy ? Autosave::kMaxCopyBytes : 0);
        {
            auto doc = Document::openOrThrow(file);
            threeEdits(*doc);
            ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
        }
        ot::paths::writeFile(file, other);  // файл на диске заменили после падения
        const auto entry = store.find(file);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->hasCopy, withCopy);
        ot::RecoverResult r = store.recover(*entry);
        if (withCopy) {
            ASSERT_TRUE(r.ok()) << r.message;
            EXPECT_TRUE(r.fromCopy);
            EXPECT_EQ(r.document->path(), file);
            EXPECT_EQ(r.document->pageCount(), 3);
            expectThreeEdits(*r.document);
        } else {
            EXPECT_EQ(r.error, RecoverError::SourceChanged) << r.message;
            EXPECT_EQ(r.document, nullptr);
        }
    }
}

TEST(autosave, journal_of_another_file_is_rejected) {
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"один.pdf", ot::test::makePdf(ot::MuContext::global(), 3));
    Autosave store(dir.path() / L"autosave", 0);
    {
        auto doc = Document::openOrThrow(file);
        threeEdits(*doc);
        ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
    }
    AutosaveEntry entry = *store.find(file);
    // Запись подменена: sha256 совпал (подделка индекса), а файл другой — MuPDF не принимает журнал.
    const std::string other = ot::test::makePdf(ot::MuContext::global(), 5);
    ot::paths::writeFile(file, other);
    entry.sha256 = ot::hash::sha256Hex(other);
    ot::RecoverResult r = store.recover(entry);
    EXPECT_EQ(r.error, RecoverError::JournalMismatch) << r.message;
}

TEST(autosave, saved_document_has_nothing_to_autosave_and_entry_is_removed) {
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"сохранён.pdf", ot::test::makePdf(ot::MuContext::global(), 2));
    Autosave store(dir.path() / L"autosave");
    auto doc = Document::openOrThrow(file);
    pageMode(*doc);
    ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
    ASSERT_TRUE(ot::Saver::save(*doc).ok());
    EXPECT_EQ(store.save(*doc), AutosaveStatus::NothingToSave);
    EXPECT_FALSE(store.find(file).has_value());
    // Не PDF и документ без пути — пропуск.
    auto blank = Document::createBlank(1);
    rotate(*blank, 0, 90);
    EXPECT_EQ(store.save(*blank), AutosaveStatus::Skipped);
}

TEST(autosave, repaired_file_is_recovered_too) {
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"обрезан.pdf",
                              ot::paths::readFile(ot::test::sourceDir() / L"tests/data/mini/truncated.pdf"));
    Autosave store(dir.path() / L"autosave");
    {
        auto doc = Document::openOrThrow(file);
        ASSERT_TRUE(doc->repaired());
        rotate(*doc, 2, 180);
        pageMode(*doc);
        ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
    }
    ot::RecoverResult r = store.recover(*store.find(file));
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.document->pageRotation(2), 180);
    EXPECT_EQ(ot::test::rootName(*r.document, "PageMode"), "UseOutlines");
}

TEST(autosave, index_json_keeps_path_sha_and_mtime) {
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"индекс.pdf", ot::test::makePdf(ot::MuContext::global(), 2));
    Autosave store(dir.path() / L"autosave");
    auto doc = Document::openOrThrow(file);
    pageMode(*doc);
    ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
    const nlohmann::json index = nlohmann::json::parse(ot::paths::readFile(store.dir() / L"index.json"));
    ASSERT_EQ(index.at("entries").size(), 1u);
    const nlohmann::json& e = index.at("entries").at(0);
    EXPECT_EQ(e.at("path").get<std::string>(), ot::paths::toUtf8(file));
    EXPECT_EQ(e.at("sha256").get<std::string>().size(), 64u);
    EXPECT_GT(e.at("mtime").get<std::int64_t>(), 1'700'000'000);
    EXPECT_EQ(e.at("key").get<std::string>(), Autosave::keyFor(file));
    // Ключ не зависит от регистра пути.
    std::wstring upper = file.wstring();
    CharUpperBuffW(upper.data(), static_cast<DWORD>(upper.size()));
    EXPECT_EQ(Autosave::keyFor(fs::path(upper)), Autosave::keyFor(file));
}

TEST(autosave, taskkill_in_the_middle_of_edits_keeps_autosaved_edits) {
    OT_FEATURE("view-27");
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"падение.pdf", ot::test::makePdf(ot::MuContext::global(), 3));
    const fs::path autosaveDir = dir.path() / L"autosave";
    PROCESS_INFORMATION pi = startChild(autosaveDir, file);
    ASSERT_NE(pi.hProcess, nullptr);
    const fs::path ready = autosaveDir / L"child.ready";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!fs::exists(ready) && std::chrono::steady_clock::now() < deadline &&
           WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(fs::exists(ready)) << "child did not autosave";
    // «Падение» посреди правок: taskkill /F.
    EXPECT_EQ(runAndWait(L"taskkill.exe /F /PID " + std::to_wstring(pi.dwProcessId)), 0);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    Autosave store(autosaveDir);
    const auto entry = store.find(file);
    ASSERT_TRUE(entry.has_value());
    ot::RecoverResult r = store.recover(*entry);
    ASSERT_TRUE(r.ok()) << r.message;
    expectThreeEdits(*r.document);
    EXPECT_EQ(r.document->pageRotation(2), 0);  // правка после автосохранения пропала вместе с процессом
}

TEST(autosave_child, DISABLED_edit_autosave_and_wait_to_be_killed) {
    // Пути с кириллицей — только широким _wgetenv (узкий getenv отдаёт ANSI).
    const wchar_t* dirEnv = _wgetenv(L"OTTISK_AUTOSAVE_CHILD_DIR");
    const wchar_t* fileEnv = _wgetenv(L"OTTISK_AUTOSAVE_CHILD_FILE");
    ASSERT_NE(dirEnv, nullptr);
    ASSERT_NE(fileEnv, nullptr);
    const fs::path dir(dirEnv);
    Autosave store(dir);
    auto doc = Document::openOrThrow(fs::path(fileEnv));
    threeEdits(*doc);
    ASSERT_EQ(store.save(*doc), AutosaveStatus::Saved);
    rotate(*doc, 2, 270);  // ещё правка, не автосохранённая
    ot::paths::writeFile(dir / L"child.ready", "1");
    std::this_thread::sleep_for(std::chrono::seconds(60));
}

// Рубеж Р1 (tools/verify_r1.py, M1 «автосохранение переживает taskkill»): AcroBatya.exe --script правит
// свойства, AutosaveRunner окна пишет журнал, скрипт рубежа снимает процесс taskkill /F, а этот помощник
// восстанавливает документ из каталога автосохранения программы (OTTISK_AUTOSAVE_CHILD_DIR,
// OTTISK_AUTOSAVE_CHILD_FILE) и печатает строку «recovered: steps=… position=… title=…» — заголовок сверяет
// скрипт; отмена шагов возвращает исходный.
TEST(autosave_child, DISABLED_recover_from_store) {
    const wchar_t* dirEnv = _wgetenv(L"OTTISK_AUTOSAVE_CHILD_DIR");
    const wchar_t* fileEnv = _wgetenv(L"OTTISK_AUTOSAVE_CHILD_FILE");
    ASSERT_NE(dirEnv, nullptr);
    ASSERT_NE(fileEnv, nullptr);
    const fs::path file(fileEnv);
    Autosave store{fs::path(dirEnv)};
    const auto entry = store.find(file);
    ASSERT_TRUE(entry.has_value()) << "no autosave entry";
    const std::string original = Document::openOrThrow(file)->properties().title;
    ot::RecoverResult r = store.recover(*entry);
    ASSERT_TRUE(r.ok()) << r.message;
    Document& doc = *r.document;
    EXPECT_TRUE(hasUnsaved(doc));
    const std::string title = doc.properties().title;
    std::printf("recovered: steps=%d position=%d title=%s\n", entry->steps, entry->position, title.c_str());
    while (doc.undoStack().canUndo()) {
        (void)doc.undoStack().undo();
    }
    EXPECT_EQ(doc.properties().title, original);
}

// ---------------------------------------------------------------- AutosaveRunner

TEST(autosave_runner, interval_is_clamped_to_1_30_minutes) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    ot::AutosaveRunner runner(qpath(dir.path() / L"autosave"));
    EXPECT_EQ(runner.intervalMinutes(), 5);
    runner.setIntervalMinutes(0);
    EXPECT_EQ(runner.intervalMinutes(), 1);
    runner.setIntervalMinutes(45);
    EXPECT_EQ(runner.intervalMinutes(), 30);
    runner.start();
    EXPECT_TRUE(runner.isActive());
    runner.stop();
    EXPECT_FALSE(runner.isActive());
}

TEST(autosave_runner, tick_saves_edited_sessions_save_and_discard_remove_entries) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"сессия.pdf", ot::test::makePdf(ot::MuContext::global(), 2));
    ot::AutosaveRunner runner(qpath(dir.path() / L"autosave"));
    ot::DocumentSession s;
    ASSERT_TRUE(s.open(qpath(file)));
    runner.watch(&s);
    int autosaved = 0;
    QObject::connect(&runner, &ot::AutosaveRunner::autosaved, [&](const QString&) { ++autosaved; });
    runner.tick();  // правок нет — ничего
    s.postSync([](ot::TaskContext&) {});
    EXPECT_FALSE(runner.store().find(file).has_value());
    s.edit(QStringLiteral("Page mode"), [](ot::TaskContext& tc) {
         pageMode(tc.document());
     }).waitForFinished();
    runner.tick();
    s.postSync([](ot::TaskContext&) {});
    EXPECT_TRUE(ot::test::waitUntil([&] { return autosaved == 1; }));
    ASSERT_TRUE(runner.store().find(file).has_value());
    runner.tick();  // правок с прошлого раза нет — не пишет снова
    s.postSync([](ot::TaskContext&) {});
    QCoreApplication::processEvents();
    EXPECT_EQ(autosaved, 1);
    // «Сохранить» удаляет запись.
    ASSERT_TRUE(s.save().result().ok());
    EXPECT_TRUE(ot::test::waitUntil([&] { return !runner.store().find(file).has_value(); }));
    // «Не сохранять» — тоже.
    s.edit(QStringLiteral("Rotate"), [](ot::TaskContext& tc) {
         rotate(tc.document(), 0, 90);
     }).waitForFinished();
    ASSERT_EQ(runner.saveNow(&s).result(), AutosaveStatus::Saved);
    ASSERT_TRUE(runner.store().find(file).has_value());
    runner.discard(&s);
    EXPECT_FALSE(runner.store().find(file).has_value());
}

TEST(autosave_runner, recover_opens_document_with_edits_in_session) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path file = put(dir.path(), L"старт.pdf", ot::test::makePdf(ot::MuContext::global(), 3));
    const QString autosaveDir = qpath(dir.path() / L"autosave");
    {
        ot::AutosaveRunner runner(autosaveDir);
        ot::DocumentSession s;
        ASSERT_TRUE(s.open(qpath(file)));
        s.edit(QStringLiteral("Edits"), [](ot::TaskContext& tc) {
             threeEdits(tc.document());
         }).waitForFinished();
        ASSERT_EQ(runner.saveNow(&s).result(), AutosaveStatus::Saved);
    }
    ot::AutosaveRunner runner(autosaveDir);
    const std::vector<AutosaveEntry> found = runner.recoverable();
    ASSERT_EQ(found.size(), 1u);
    ot::DocumentSession s;
    const ot::OpenSummary summary = runner.recover(found.front(), &s).result();
    ASSERT_TRUE(summary.ok) << summary.error.message.toStdString();
    EXPECT_EQ(s.path(), qpath(file));
    EXPECT_EQ(s.pageCount(), 3);
    EXPECT_TRUE(s.undoState().canUndo);
    s.postSync([](ot::TaskContext& tc) { expectThreeEdits(tc.document()); });
}
