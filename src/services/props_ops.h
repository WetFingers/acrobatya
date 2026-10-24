// Свойства документа через DocumentSession (план 2.4b — doc.setProperty; 2.5 — диалог «Свойства» Ctrl+D и
// редактор XMP): задачи потока документа над Document::properties/setProperties и core/doc/props.h (/Info и
// XMP вместе), core/links/initial_view.h (начальный вид, Base URL). Правка — один шаг журнала («Set document
// property», «Document properties»: undo возвращает прежние /Info, XMP, /Lang, начальный вид и Base URL). ui
// держит только значения.
#pragma once

#include <QFuture>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/props.h"
#include "core/links/initial_view.h"
#include "services/document_session.h"

namespace ot::props {

// Ключи setProperty: title, author, subject, keywords, creator, producer.
QStringList editableKeys();

QFuture<DocProperties> read(DocumentSession& session);
// Записать одно поле /Info (и XMP); неизвестный ключ — исключение std::invalid_argument в QFuture.
QFuture<void> setProperty(DocumentSession& session, const QString& key, const QString& value);

// Права документа (Document::allowed; владелец — всё).
struct Rights {
    bool print = true;
    bool printHighRes = true;
    bool copy = true;
    bool edit = true;
    bool annotate = true;
    bool fillForms = true;
    bool accessibility = true;
    bool assemble = true;
};

// Всё для диалога «Свойства документа» (без шрифтов и pdf_check_document — они дольше: fonts, check).
struct Sheet {
    QString path;
    bool isPdf = false;
    DocProperties doc;  // формат, версия, линеаризация, шифрование, /P
    InfoFields info;    // /Info с пользовательскими ключами
    std::string xmp;    // пакет XMP (пусто — нет)
    std::string lang;
    std::string baseUrl;
    InitialView initial;
    Advanced advanced;  // без проверки
    Rights rights;
    QSizeF firstPageSize;  // пункты
};
QFuture<Sheet> readSheet(DocumentSession& session);
// Шрифты всех страниц (вкладка «Шрифты» — по первому показу).
QFuture<std::vector<FontInfo>> fonts(DocumentSession& session);
// «Дополнительно → Проверить»: pdf_check_document (читает все объекты; ремонт сбрасывает историю отмены).
QFuture<Advanced> check(DocumentSession& session);

// Изменения диалога: что задано — записывается одним шагом журнала «Document properties».
struct Changes {
    std::optional<InfoFields> info;
    std::optional<InitialView> initial;
    std::optional<std::string> lang;
    std::optional<std::string> baseUrl;
    bool empty() const noexcept { return !info && !initial && !lang && !baseUrl; }
};
QFuture<void> apply(DocumentSession& session, const Changes& changes);

// Свойство XMP для редактора «Дополнительные метаданные»: пространство имён (URI и привычный префикс), имя,
// значение (rdf:Alt — x-default, Seq/Bag — через «; »).
struct XmpProperty {
    QString ns;
    QString prefix;
    QString name;
    QString value;
};
// Простые свойства пакета по известным схемам (dc, xmp, pdf, pdfx, pdfaid, xmpMM, photoshop, xmpRights);
// неверный XML — пусто.
QVector<XmpProperty> xmpProperties(const std::string& packet);

}  // namespace ot::props
