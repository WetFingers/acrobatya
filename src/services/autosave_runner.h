// AutosaveRunner — автосохранение открытых документов по таймеру (эталон 4.5, view-27): раз в N минут (1–30,
// по умолчанию 5) каждой отслеживаемой сессии с правками после прошлого автосохранения — задача Background в
// её потоке (Autosave::save: журнал MuPDF + копия исходника + index.json). «Сохранить» (сигнал
// DocumentSession::saved) и «Не сохранять» (discard) удаляют запись; при старте — recoverable() для
// «Восстановить несохранённые изменения?» и recover(entry, session) — документ с правками и историей
// открывается в сессии. Каталог — app::dataDir()/autosave (тесты — OTTISK_DATA_DIR). Живёт в GUI-потоке.
#pragma once

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

#include "core/doc/autosave.h"
#include "services/document_session.h"

namespace ot {

class AutosaveRunner : public QObject {
    Q_OBJECT
public:
    static constexpr int kDefaultMinutes = 5;

    explicit AutosaveRunner(const QString& dir = defaultDir(), QObject* parent = nullptr);
    static QString defaultDir();

    // Интервал в минутах, приводится к 1–30; таймер перезапускается.
    void setIntervalMinutes(int minutes);
    int intervalMinutes() const noexcept { return minutes_; }
    void start();
    void stop();
    bool isActive() const { return timer_.isActive(); }

    void watch(DocumentSession* session);
    void unwatch(DocumentSession* session);

    // Автосохранить отслеживаемые сессии с новыми правками (это делает таймер).
    void tick();
    // Автосохранить одну сессию сейчас (правки есть или нет — решает Autosave::save).
    QFuture<AutosaveStatus> saveNow(DocumentSession* session);
    // «Не сохранять»: удалить запись документа сессии.
    void discard(DocumentSession* session);
    void discard(const QString& documentPath);

    std::vector<AutosaveEntry> recoverable() const;
    // Открыть в сессии документ с восстановленными правками.
    QFuture<OpenSummary> recover(const AutosaveEntry& entry, DocumentSession* session,
                                 const QString& password = {});

    const Autosave& store() const noexcept { return *store_; }

signals:
    void autosaved(const QString& path);
    void autosaveFailed(const QString& path, const QString& message);

private:
    struct Watched {
        QPointer<DocumentSession> session;
        quint64 revision = 0;  // номер правки при прошлом автосохранении
        QString path;          // путь при подключении / после сохранения (для «Сохранить как»)
    };

    void onSaved(DocumentSession* session, const QString& path);

    std::shared_ptr<Autosave> store_;  // задачи в потоках сессий держат свою ссылку
    QTimer timer_;
    int minutes_ = kDefaultMinutes;
    QHash<quint64, Watched> watched_;  // по id сессии
};

}  // namespace ot
