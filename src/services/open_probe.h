// Проба файла для --open-all (эталон 3.18, 9.6 — M1 «корпус открывается без падений»): DocumentSession
// открывает файл, рисует первую страницу в 32 × 32 и закрывается. Итог — открыт / отказ / сбой. Отказ — файл
// битый или не поддерживается (при открытии MuPDF отвечает FORMAT, SYNTAX, LIMIT, UNSUPPORTED, LIBRARY,
// GENERIC, SYSTEM — I/O и память, нужен пароль, срок вышел): так программа и должна отвечать на мутанты и
// фаззы. Открылся, но страница 1 не рисуется (любая ошибка MuPDF) — «открыт» с причиной в reason. Сбой —
// ошибка нашего кода: исключение не из MuPDF или FZ_ERROR_ARGUMENT при открытии (неверный вызов). Настоящее
// падение процесса — код выхода процесса, а не сбой здесь. Поток: любой без цикла событий (ждёт QFuture задач
// сессии).
#pragma once

#include <QString>

namespace ot {

struct OpenProbe {
    enum class Result { Opened, Refused, Failed };
    Result result = Result::Failed;
    int pages = 0;
    bool repaired = false;
    bool rendered = false;  // первая страница нарисована
    QString reason;         // английский: «damaged: …», «password», «timeout», «our code: …»
};

// timeoutMs — на открытие и на рендер по отдельности (дальше — cancelAll и «timeout», отказ).
OpenProbe probeOpen(const QString& path, int timeoutMs = 20000);

}  // namespace ot
