// SingleInstance — один экземпляр программы на пользователя (эталон 3.1 «Открытие», план 2.3; view-27):
// первый слушает QLocalServer «AcroBatya-<SID>-<экземпляр>» (именованный канал, доступ — только этому
// пользователю), второй передаёт ему файлы и выходит. Экземпляр — переменная OTTISK_INSTANCE (тесты, снимки,
// --script,
// --open-all и очередь — «test»; без переменной — «main»): offscreen-прогон не отдаёт файл «АкроБате»,
// открытому у человека, и не закрывается из-за него. Протокол: одна строка JSON {"files": [абсолютные пути],
// "page": N} и перевод строки; ответ "ok\n" — принято. Поток: GUI (сигнал requestReceived — в потоке
// объекта).
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

namespace ot {

class SingleInstance : public QObject {
    Q_OBJECT
public:
    struct Request {
        QStringList files;  // абсолютные пути
        int page = 0;       // --page N (1 — первая); 0 — как обычно
        bool operator==(const Request&) const = default;
    };

    // AcroBatya-<SID пользователя>-<OTTISK_INSTANCE или main>.
    static QString serverName();
    static QString userSid();   // S-1-5-21-…; не удалось — имя пользователя
    static QString instance();  // OTTISK_INSTANCE или "main"

    // Передать запрос запущенному экземпляру и дождаться ответа; true — принят (второму экземпляру выйти).
    static bool sendToRunning(const Request& request, const QString& name = serverName(),
                              int timeoutMs = 3000);

    static QByteArray encode(const Request& request);
    static bool decode(const QByteArray& line, Request* request);

    explicit SingleInstance(QString name = serverName(), QObject* parent = nullptr);
    ~SingleInstance() override;

    // Стать первым экземпляром; false — имя занято или ошибка (errorString()).
    bool listen();
    bool isListening() const;
    QString name() const { return name_; }
    QString errorString() const { return error_; }

signals:
    void requestReceived(const ot::SingleInstance::Request& request);

private:
    void accept();
    void readFrom(QLocalSocket* socket);

    QString name_;
    QString error_;
    QLocalServer* server_ = nullptr;
};

}  // namespace ot

Q_DECLARE_METATYPE(ot::SingleInstance::Request)
