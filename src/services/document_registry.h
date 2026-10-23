// DocumentRegistry — открытые документы по id вкладки (эталон 4.2, view-27): у каждого документа своя
// DocumentSession — свой поток и свой контекст MuPDF (клон global()), документ — shared_ptr<Document>
// внутри сессии, указатели MuPDF наружу не выходят. Один документ — одна вкладка: тот же файл второй раз
// даёт прежний id. Закрытие — после опустошения очереди задач документа. Ошибка или падение задачи в одном
// документе не трогает остальные.
// Поток: методы реестра — из GUI-потока (сессии создаются и разрушаются здесь).
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <map>
#include <memory>

#include "services/document_session.h"

namespace ot {

class DocumentRegistry : public QObject {
    Q_OBJECT
public:
    struct Opened {
        quint64 id = 0;         // 0 — не открылся (summary.error)
        bool existing = false;  // уже был открыт — та же вкладка
        OpenSummary summary;
    };

    explicit DocumentRegistry(QObject* parent = nullptr);
    ~DocumentRegistry() override;

    // Открыть файл в новой сессии (с ожиданием); тот же путь уже открыт — его id и existing = true.
    Opened open(const QString& path, const QString& password = {});
    // Сессия вкладки; nullptr — нет такой.
    std::shared_ptr<DocumentSession> session(quint64 id) const;
    // id вкладки с этим файлом (сравнение без регистра и с нормализацией пути); 0 — не открыт.
    quint64 findByPath(const QString& path) const;
    QList<quint64> ids() const;
    int count() const { return static_cast<int>(sessions_.size()); }

    // Закрыть после уже поставленных задач; false — нет такого id.
    bool close(quint64 id);
    void closeAll();

    // Ключ сравнения путей (абсолютный, «/», нижний регистр — Windows).
    static QString pathKey(const QString& path);

signals:
    void documentOpened(quint64 id);
    void documentClosed(quint64 id);

private:
    std::map<quint64, std::shared_ptr<DocumentSession>> sessions_;
};

}  // namespace ot
