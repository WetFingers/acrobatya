#include "ui/shell/undo_titles.h"

#include <QCoreApplication>

namespace ot::ui {

namespace {

struct StepTitle {
    const char* code;
    const char* title;
};

constexpr StepTitle kStepTitles[] = {
    {"Add note", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "записка")},
    {"Add text box", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "текстовое поле")},
    {"Add line", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "линия")},
    {"Add rectangle", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "прямоугольник")},
    {"Add oval", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "овал")},
    {"Add polygon", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "многоугольник")},
    {"Add polyline", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "ломаная")},
    {"Add highlight", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "выделение текста")},
    {"Add underline", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "подчёркивание текста")},
    {"Add squiggly", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "волнистое подчёркивание")},
    {"Add strikeout", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "зачёркивание текста")},
    {"Add stamp", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "штамп")},
    {"Insert text", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "вставка текста")},
    {"Replace text", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "замена текста")},
    {"Add ink", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "штрих")},
    {"Erase ink", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "ластик")},
    {"Add attachment", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "вложение")},
    {"Edit note text", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "текст записки")},
    {"Set annotation properties", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "свойства комментария")},
    {"Set annotation flags", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "свойства комментария")},
    {"Set annotation rectangle", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "перемещение комментария")},
    {"Move annotation", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "перемещение комментария")},
    {"Delete annotation", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "удаление комментария")},
    {"Paste annotations", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "вставка комментариев")},
    {"Edit text", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "правка текста")},
    {"Add text", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "новый текст")},
    {"Replace all", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "замена всех")},
    {"Add image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "изображение")},
    {"Move image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "перемещение изображения")},
    {"Resize image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "размер изображения")},
    {"Rotate image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "поворот изображения")},
    {"Flip image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "отражение изображения")},
    {"Crop image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "обрезка изображения")},
    {"Replace image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "замена изображения")},
    {"Delete image", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "удаление изображения")},
    {"Document properties", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "свойства документа")},
    {"Set document property", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "свойства документа")},
    {"Clear signature", QT_TRANSLATE_NOOP("ot::ui::UndoTitles", "очистка подписи")},
};

}  // namespace

QString undoStepTitle(const QString& code) {
    for (const StepTitle& s : kStepTitles) {
        if (code == QLatin1String(s.code)) {
            return QCoreApplication::translate("ot::ui::UndoTitles", s.title);
        }
    }
    return {};
}

}  // namespace ot::ui
