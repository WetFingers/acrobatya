// mu::call — единственный вход в MuPDF из C++ (эталон 4.1, страж fztry_guard: fz_try/fz_catch живут
// только в src/core/mu/). Тело выполняется внутри fz_try; ошибка MuPDF становится ot::MuError.
//
// Правила для тела (лямбды) — их проверяет tools/fztry_guard.py:
//  * только вызовы MuPDF и присваивания в переменные, объявленные СНАРУЖИ (по ссылке), в том числе
//    handle.reset(…) у RAII-обёрток core/mu/handles.h, объявленных снаружи;
//  * никаких объектов C++ с деструкторами внутри тела (std::string, std::vector, mu::*Handle…): fz_try —
//    это setjmp/longjmp, при ошибке деструкторы не выполняются;
//  * нет return, throw и вложенного mu::call: исключение C++ не должно пересекать кадр setjmp;
//  * освобождение (fz_drop_*) — после mu::call или через RAII-обёртки: drop-функции не бросают.
// Ошибка уходит в журнал колбэком контекста (fz_report_error → spdlog); отмена (FZ_ERROR_ABORT,
// FZ_ERROR_TRYLATER) не журналируется — fz_ignore_error.
#pragma once

#include <cstring>

#include "core/mu/error.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot::mu {

#pragma warning(push)
// C4611: interaction between '_setjmp' and C++ object destruction is non-portable — тело не держит
// объектов с деструкторами (см. правила выше), сам call держит только POD.
#pragma warning(disable : 4611)

template <class F>
void call(fz_context* ctx, F&& body) {
    int code = 0;
    char message[512];
    message[0] = '\0';
    fz_try(ctx) {
        body();
    }
    fz_catch(ctx) {
        code = fz_caught(ctx);
        const char* text = fz_caught_message(ctx);
        strncpy_s(message, sizeof(message), text ? text : "MuPDF error", _TRUNCATE);
        if (code == FZ_ERROR_ABORT || code == FZ_ERROR_TRYLATER) {
            fz_ignore_error(ctx);
        } else {
            fz_report_error(ctx);
        }
    }
    if (code != 0) {
        throw MuError(code, message);
    }
}

#pragma warning(pop)

}  // namespace ot::mu
