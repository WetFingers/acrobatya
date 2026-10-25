// Измерения через DocumentSession (план 5.5; эталон 3.4 «Измерения»; comment-29, comment-30, comment-40):
// задачи потока документа над core/annot/measure.h — привязки страницы (вершины и концы путей), видовые
// экраны /VP, пометка-измерение одним шагом истории («Add distance», «Add perimeter», «Add area»), все
// измерения документа (экспорт CSV); настройки «Измерения (2D)» (measuring/… services/settings_keys.h).
#pragma once

#include <QByteArray>
#include <QFuture>
#include <QLocale>
#include <QString>
#include <QStringList>

#include <vector>

#include "core/annot/annots.h"
#include "core/annot/measure.h"
#include "core/util/geometry.h"
#include "services/document_session.h"

namespace ot {
class Settings;
}

namespace ot::measure {

QFuture<std::vector<PointF>> snapPoints(DocumentSession& session, int page,
                                        TaskPriority priority = TaskPriority::High);
QFuture<std::vector<Viewport>> viewports(DocumentSession& session, int page,
                                         TaskPriority priority = TaskPriority::High);
QFuture<annot::AnnotRef> create(DocumentSession& session, int page, MeasureProps props);
QFuture<std::vector<Measurement>> list(DocumentSession& session,
                                       TaskPriority priority = TaskPriority::Normal);

// Концы линии расстояния (measuring/lineEnds).
enum class LineEnds { Arrow, Tick, None };

struct MeasureSettings {
    Scale scale;                   // масштаб пользователя (measuring/scale, measuring/precision)
    bool useDocumentScale = true;  // /VP документа важнее
    bool snap = true;
    bool ortho = false;
    LineEnds lineEnds = LineEnds::Arrow;
    float leaderLength = 0;
    float leaderExtension = 0;
    float leaderOffset = 0;
    bool captionTop = false;
    bool showInfo = true;
};

MeasureSettings settings(const Settings& settings);
void setSettings(Settings& settings, const MeasureSettings& value);
// Только масштаб и точность (диалог «Масштаб и точность»).
void setScale(Settings& settings, const Scale& scale);
// /LE расстояния по настройке: стрелки — OpenArrow, засечки — Butt.
annot::LineEnding lineEnding(LineEnds ends) noexcept;

// Экспорт измерений в CSV (comment-40): строка на измерение — страница (с 1), тип, значение (точность
// измерения, десятичный разделитель локали), единицы, подпись. Подписи типов и единиц — на языке интерфейса
// (готовит ui).
struct CsvRow {
    int page = 1;
    QString kind;
    double value = 0;
    bool hasValue = true;  // нет /Measure — пустое значение
    int precision = 2;
    QString unit;
    QString caption;
};
// Разделитель полей по локали (как список Excel): десятичная запятая — «;», иначе «,».
QChar csvSeparator(const QLocale& locale);
// UTF-8 с BOM (Excel узнаёт кодировку), строки — CRLF; поле с разделителем, кавычкой или переводом строки — в
// кавычках (кавычка удваивается); текст, начинающийся с = + - @, — с апострофом (не формула в Excel).
QByteArray toCsv(const std::vector<CsvRow>& rows, const QStringList& header, const QLocale& locale);

}  // namespace ot::measure
