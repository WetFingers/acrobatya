// «Прочитанные» комментарии (план 5.4; эталон 3.4 «Панель «Комментарии»», 4.7): локально в state.sqlite
// (таблица comments_read, миграция 7) по {ключ документа, /NM}; в файл ничего не пишется. Ключ документа —
// core/annot/comments.h commentsDocumentKey (SHA-256 первого /ID), без /ID — SHA-256 полного пути. Запись без
// отметки: свой комментарий (автор = me) — прочитан, чужой — нет (как у Acrobat: новые комментарии
// рецензентов выделены, пока их не открыли). Запись без /NM — по «#страница:объект».
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <string>

#include "core/annot/comments.h"

namespace ot {

class StateDb;

namespace comments {

class ReadState {
public:
    // Подключение state.sqlite (по умолчанию — StateDb::shared()).
    ReadState();
    explicit ReadState(StateDb& db);

    // Ключ документа: idKey (commentsDocumentKey), иначе SHA-256 пути без учёта регистра; оба пусты — пусто
    // (документ «Без имени» — отметки только в памяти этого объекта).
    static QString documentKey(const std::string& idKey, const QString& path);
    // Ключ записи: /NM или «#страница:объект».
    static QString commentKey(const annot::AnnotRef& ref);

    void setDocument(const QString& documentKey);
    QString document() const { return doc_; }

    // Отметка записи (nullopt — не отмечалась).
    std::optional<bool> stored(const annot::AnnotRef& ref) const;
    // Прочитана ли: отметка, иначе — своя (author == me).
    bool isRead(const annot::Comment& comment, const std::string& me) const;
    void setRead(const annot::AnnotRef& ref, bool read);
    void setRead(const std::vector<annot::AnnotRef>& refs, bool read);
    // Все отметки документа (перечитать из базы).
    void reload();

private:
    StateDb* db_ = nullptr;
    QString doc_;
    QHash<QString, bool> marks_;  // ключ записи → прочитана
};

}  // namespace comments
}  // namespace ot
