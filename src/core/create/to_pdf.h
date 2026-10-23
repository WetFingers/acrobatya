// «Сохранить как PDF» для документа «только просмотр» (эталон 3.1 «Открытие», план 2.3; view-01): XPS, EPUB,
// CBZ, FB2, изображения, TXT, HTML — каждая страница идёт через fz_run_page в fz_document_writer «pdf» (текст
// остаётся текстом, векторы — векторами, картинки — как есть). Аннотации не-PDF нет; у PDF на входе страницы
// переписываются вместе с внешним видом аннотаций (это не сохранение — для PDF есть Saver). Диспетчер
// создания с параметрами — сессия 7.2 (create-01).
// Поток: как у Document — в потоке документа (задачей DocumentSession, services/convert_ops.h).
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ot {

class Document;

// PDF в памяти; cancelled() == true между страницами — MuError(FZ_ERROR_ABORT).
std::string renderToPdf(const Document& doc, const std::function<bool()>& cancelled = {});
// То же в файл (каталоги создаются); при ошибке и отмене файла нет.
void writeAsPdf(const Document& doc, const std::filesystem::path& out,
                const std::function<bool()>& cancelled = {});

}  // namespace ot
