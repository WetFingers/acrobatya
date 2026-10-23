// Базовые действия --script (план 2.4b; эталон 3.18, 9.6) над главным окном — то, чем рубежи открывают,
// листают, правят, отменяют и снимают документ:
//   doc.open{path, password?, page?}   doc.save{path?, mode?}   doc.saveCopy{path, mode?}   doc.close
//   doc.undo{n?}   doc.redo{n?}   doc.setProperty{key, value}   (mode: auto|incremental|full|optimized)
//   view.zoom{percent}   view.zoomFit{mode?: page|width|height|visible|actual}   view.zoomIn   view.zoomOut
//   view.page{page}   view.layout{mode, cover?}   view.rotate{degrees}
//   command.run{id}   — команда реестра окна (меню, клавиши) по id: «view.readMode», «app.preferences»…
//   shot{name}        — PNG окна в ScriptContext::shotsDir() (--out), когда видимые тайлы готовы
//   wait{ms}          — пауза с циклом событий (фоновая работа, анимации)
// Страницы — с 1, как в интерфейсе. Документ правится только через DocumentSession; ожидание будущих — с
// циклом событий (сигналы окна доходят). Действия инструментов регистрируют сессии-владельцы своими файлами
// рядом (рецепт «Новое действие --script» docs/map.md).
#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QString>
#include <QThread>

#include <type_traits>

#include "services/script_runner.h"

namespace ot::ui {

class DocumentView;
class MainWindow;

void registerBaseActions(ScriptRunner& runner, MainWindow& window);

// Помощники действий (и для действий сессий-владельцев).
namespace script {

// Доставить отложенные события (сигналы потоков, раскладка) — после действия, которое меняет окно.
void settle();
// Текущий документ окна; нет — ScriptError «no open document».
DocumentView& document(MainWindow& window);
// Видимые тайлы текущего документа дорисованы (до 30 с); нет документа — true.
bool tilesReady(MainWindow& window);

// Дождаться задачи документа (DocumentSession::post/edit, *_ops.h) с циклом событий; исключение задачи —
// наружу, дольше 120 с — ScriptError.
template <class T>
T await(QFuture<T> future, const char* what) {
    QElapsedTimer t;
    t.start();
    while (!future.isFinished()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (t.elapsed() > 120000) {
            throw ScriptError(QStringLiteral("%1: timed out").arg(QLatin1String(what)));
        }
        QThread::msleep(1);
    }
    if constexpr (std::is_void_v<T>) {
        future.waitForFinished();  // исключение задачи
        settle();
    } else {
        T value = future.result();
        settle();
        return value;
    }
}

}  // namespace script

// Режим AcroBatya.exe --script файл.json [--out каталог]: окно 1440 × 900 (под offscreen — без экрана),
// вопросы «Сохранить изменения?» — «Не сохранять», шаги, итог в stdout. Код 0 — все шаги выполнены, 1 —
// ошибка шага или файла (строка «step N cmd "name": …»).
int runScript(const QString& file, const QString& outDir);

}  // namespace ot::ui
