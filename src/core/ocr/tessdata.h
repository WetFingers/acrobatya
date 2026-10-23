// Языки OCR на диске (эталон 3.18 --self-check «tessdata — загрузкой», 10.3): каталог tessdata рядом с
// программой ({app}\tessdata — так кладёт установщик), иначе TESSDATA_PREFIX; проверка — настоящая загрузка
// языка Tesseract из состава MuPDF (ocr_init/ocr_fin MuPDF — тот же путь, что у fz_new_ocr_device; свой
// контекст и память Leptonica через MuPDF). Узкий fopen Tesseract (ACP) — путь через короткое имя 8.3. Движок
// OCR целиком — сессия 8.1 (core/ocr/engine.h).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ot::ocr {

struct TessdataProbe {
    bool ok = false;
    std::filesystem::path dir;
    std::vector<std::string> languages;  // *.traineddata в каталоге (rus, eng, deu…)
    std::string error;                   // английский текст
};

// Каталог tessdata: <exeDir>/tessdata, если есть *.traineddata; иначе TESSDATA_PREFIX; пусто — не найден.
std::filesystem::path findTessdata(const std::filesystem::path& exeDir);
// Языки каталога (имена *.traineddata без расширения, по алфавиту).
std::vector<std::string> tessdataLanguages(const std::filesystem::path& dir);
// Загрузить язык lang из dir (TessBaseAPI::Init) и сразу освободить.
TessdataProbe probeTessdata(const std::filesystem::path& dir, const std::string& lang = "rus");

}  // namespace ot::ocr
