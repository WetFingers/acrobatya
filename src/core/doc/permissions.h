// Права документа /P (план 2.5b; эталон 7.1; security-08) — то, что соблюдают все команды: печать,
// копирование, правка, комментарии, формы, сборка страниц, чтение с экрана. Биты — ISO 32000-1 таблица 22
// (нумерация с 1): 3 печать · 4 правка содержимого · 5 копирование · 6 комментарии и формы · с R ≥ 3 ещё
// 9 заполнение форм · 10 извлечение для доступности · 11 сборка документа · 12 печать высокого качества.
// Действующие права (как у Acrobat): формы можно заполнять при бите 9 или 6, собирать страницы — при бите 11
// или 4, печать высокого качества — только вместе с печатью; у R2 битов 9–12 нет: доступность идёт за
// копированием, сборка — за правкой, высокое качество — за печатью. Файл без шифрования и открытый паролем
// владельца — всё разрешено. MuPDF права не соблюдает сам — это делает программа (ui: серые команды с
// подсказкой «Документ защищён: …»).
#pragma once

#include <string_view>

#include "core/doc/document.h"  // Perm

namespace ot {

inline constexpr int kPermBitPrint = 3;
inline constexpr int kPermBitModify = 4;
inline constexpr int kPermBitCopy = 5;
inline constexpr int kPermBitAnnotate = 6;
inline constexpr int kPermBitFillForms = 9;
inline constexpr int kPermBitAccessibility = 10;
inline constexpr int kPermBitAssemble = 11;
inline constexpr int kPermBitPrintHighRes = 12;

struct Permissions {
    bool print = true;
    bool printHighRes = true;
    bool copy = true;
    bool edit = true;
    bool annotate = true;
    bool fillForms = true;
    bool accessibility = true;
    bool assemble = true;

    // Действующие права по /P и /R стандартного обработчика (правила выше).
    static Permissions fromP(int p, int revision) noexcept;
    // Права открытого документа (не PDF, без шифрования, пароль владельца — всё).
    static Permissions of(const Document& doc);

    bool allows(Perm perm) const noexcept;
    // Хоть одно право снято.
    bool restricted() const noexcept;
    // Чтение текста экранным диктором: бит доступности или копирование.
    bool screenReader() const noexcept { return accessibility || copy; }
};

// Код права для журнала и ключей: "print", "printHighRes", "copy", "edit", "annotate", "fillForms",
// "accessibility", "assemble".
std::string_view permName(Perm perm) noexcept;

inline constexpr Perm kAllPerms[] = {Perm::Print,    Perm::PrintHighRes, Perm::Copy,          Perm::Edit,
                                     Perm::Annotate, Perm::FillForms,    Perm::Accessibility, Perm::Assemble};

}  // namespace ot
