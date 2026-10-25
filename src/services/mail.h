// Почта (эталон 3.18 «Почта», cloud-08; план 6.1, п. 4 — ядро для mailto: отправки формы; команда «Поделиться →
// Отправить по почте» — 9.1): письмо с вложениями через Simple MAPI — MAPISendMailW из MAPI32.DLL (заглушка
// Windows передаёт его почтовой программе по умолчанию) с окном письма (MAPI_DIALOG): человек видит письмо и сам
// нажимает «Отправить». Нет почтовой программы — NoClient (интерфейс честно сообщает), человек закрыл окно —
// Cancelled. mailer() — экземпляр процесса; тесты подменяют его FakeMailer (ScopedMailer): живая почта в тестах
// не открывается.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <mutex>
#include <vector>

namespace ot {

struct MailMessage {
    struct Attachment {
        QString path;  // файл на диске (временный — удаляет вызывающий после send)
        QString name;  // имя во вложении
    };
    QStringList to;
    QStringList cc;
    QStringList bcc;
    QString subject;
    QString body;
    QList<Attachment> attachments;
};

struct MailResult {
    enum class Status { Sent, Cancelled, NoClient, Failed };
    Status status = Status::Failed;
    QString message;  // английский текст для журнала
    bool ok() const noexcept { return status == Status::Sent; }
};
const char* mailStatusName(MailResult::Status status) noexcept;

class Mailer {
public:
    virtual ~Mailer() = default;
    // Синхронно (окно письма MAPI — модальное); вызывать из потока интерфейса.
    virtual MailResult send(const MailMessage& message) = 0;
};

// MAPISendMailW с окном письма; hwnd — окно-владелец (0 — без владельца).
class MapiMailer : public Mailer {
public:
    explicit MapiMailer(quintptr hwnd = 0) : hwnd_(hwnd) {}
    MailResult send(const MailMessage& message) override;
    void setOwner(quintptr hwnd) noexcept { hwnd_ = hwnd; }

private:
    quintptr hwnd_;
};

// Фейк для тестов: письма и содержимое вложений (читается при send — временные файлы потом удаляются).
class FakeMailer : public Mailer {
public:
    MailResult send(const MailMessage& message) override;
    void setResult(MailResult::Status status) { status_ = status; }
    std::vector<MailMessage> messages() const;
    QList<QByteArray> attachmentBytes() const;

private:
    mutable std::mutex mutex_;
    MailResult::Status status_ = MailResult::Status::Sent;
    std::vector<MailMessage> messages_;
    QList<QByteArray> bytes_;
};

Mailer& mailer();
class ScopedMailer {
public:
    explicit ScopedMailer(Mailer& mailer);
    ~ScopedMailer();
    ScopedMailer(const ScopedMailer&) = delete;
    ScopedMailer& operator=(const ScopedMailer&) = delete;

private:
    Mailer* previous_;
};

}  // namespace ot
