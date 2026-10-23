// DocumentSession — документ, открытый в программе (эталон 4.1–4.2). Этап 0: синхронный — открыть,
// число страниц, растр страницы в QImage. Сессия 1.2 переводит её на поток документа и пул рендера;
// интерфейс ui → DocumentSession остаётся запросами.
#pragma once

#include <QImage>
#include <QObject>
#include <QSizeF>
#include <QString>

#include <memory>

namespace ot {

class MuContext;
class Document;

class DocumentSession : public QObject {
    Q_OBJECT
public:
    explicit DocumentSession(QObject* parent = nullptr);
    ~DocumentSession() override;

    // Открыть файл; false — ошибка (текст — errorString()). Пароль не спрашивается (сессия 2.5).
    bool open(const QString& path);
    void close();

    bool isOpen() const;
    QString path() const { return path_; }
    QString errorString() const { return error_; }
    int pageCount() const;
    QSizeF pageSizePoints(int index) const;
    // Растр страницы при dpi; пустой QImage — ошибка (текст — errorString()).
    QImage renderPage(int index, qreal dpi);

signals:
    void opened(const QString& path);
    void closed();

private:
    std::unique_ptr<MuContext> mu_;
    std::unique_ptr<Document> doc_;
    QString path_;
    QString error_;
};

}  // namespace ot
