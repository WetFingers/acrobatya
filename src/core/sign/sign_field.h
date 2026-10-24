// Общий ход подписи поля /Sig (план 4.2, п. 1–2; эталон 3.8 «Механика», 4.6) — для core/sign: подпись
// (signer.h) и метка времени документа (doc_timestamp.h) различаются подписантом, внешним видом и патчем
// словаря /V. Поле — существующее пустое /Sig или новое; несохранённое, новое или восстановленное — сначала
// обычная запись по правилам Saver; затем одна операция журнала: /Lock, внешний вид, before (каталог),
// pdf_sign_signature_with_appearance, patch (/V в секции подписи) — и Saver::Incremental. Сбой — откат к
// файлу (Saver::revert) и SignError; причина подписанта — его error().
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "core/doc/saver.h"
#include "core/sign/signer.h"
#include "core/util/geometry.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign::detail {

struct FieldJob {
    std::string operation = "Sign document";  // имя шага журнала
    SignaturePlacement placement;
    bool lock = false;      // /Lock <</Type /SigFieldLock /Action /All>> до подписи
    std::int64_t time = 0;  // /M (MuPDF ставит его всегда)
    // Внешний вид видимого поля по его рамке (координаты страницы) — новый display list (освобождает
    // вызывающий); пусто — без вида. Сам зовёт mu::call.
    std::function<fz_display_list*(fz_context*, fz_rect, fz_text_language)> appearance;
    // До подписи (та же инкрементальная секция): каталог и т. п.
    std::function<void(fz_context*, pdf_document*)> before;
    // После pdf_sign_signature_with_appearance, в секции подписи: словарь /V; locked — у поля есть /Lock.
    std::function<void(fz_context*, pdf_document*, pdf_obj* v, bool locked)> patch;
};

struct FieldOutcome {
    std::string field;  // полное имя поля
    int page = 0;
    RectF rect;  // пусто — невидимое
    bool visible = false;
    SaveReport save;
};

FieldOutcome signField(Document& doc, const FieldJob& job, PdfSigner& signer,
                       const std::filesystem::path& target);

// /Extensions /ESIC <</BaseVersion /1.7 /ExtensionLevel level>> не ниже level (1 — DSS и DocTimeStamp, 2 —
// CAdES) и /Version /1.7 каталога, если файл старше. Массив расширений (PDF 2.0) не трогается. Внутри
// операции журнала, до подписи.
void ensureEsic(fz_context* ctx, pdf_document* pdf, int level);

// Перед дописыванием секции нужна обычная запись: несохранённые правки, файл нельзя дописать (ремонт) или
// документа нет на диске.
bool needsPreSave(Document& doc);

// Поле по полному имени (UTF-8); nullptr — нет.
pdf_obj* lookupField(fz_context* ctx, pdf_document* pdf, const std::string& name);
// Свободное имя «<prefix>1», «<prefix>2»…
std::string nextFieldName(Document& doc, const std::string& prefix);

}  // namespace ot::sign::detail
