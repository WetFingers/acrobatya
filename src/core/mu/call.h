// mu::call — единственный вход в MuPDF из C++ (эталон 4.1, страж fztry_guard: fz_try/fz_catch живут
// только в src/core/mu/). Тело выполняется внутри fz_try; ошибка MuPDF становится ot::MuError.
//
// Правила для тела (лямбды):
//  * только вызовы MuPDF и присваивания в переменные, объявленные СНАРУЖИ (по ссылке);
//  * никаких объектов C++ с деструкторами внутри тела: fz_try — это setjmp/longjmp, при ошибке
//    деструкторы не выполняются;
//  * не бросать исключения C++ из тела и не вызывать то, что их бросает;
//  * освобождение (fz_drop_*) — после mu::call: drop-функции не бросают.
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
        fz_report_error(ctx);
    }
    if (code != 0) {
        throw MuError(code, message);
    }
}

#pragma warning(pop)

}  // namespace ot::mu
