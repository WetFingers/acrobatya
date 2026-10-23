// Стек полос сообщений над документом (эталон 6.1 «Документ», 6.6; план 2.4): полоса на вид сообщения — не
// больше одной (повторный показ меняет текст и кнопки), порядок — по важности, сверху вниз:
//   защищённый просмотр (жёлтая) → подпись недействительна (красная) → файл восстановлен (жёлтая) →
//   JavaScript отключён (жёлтая) → подпись с предупреждением (жёлтая) → подписано (синяя) → PDF/A (синяя) →
//   форма (фиолетовая) → похоже на скан (синяя) → только просмотр (синяя) → прочие (по порядку показа).
// Компонент — здесь (2.4), наполнение — сессии-владельцы: защищённый просмотр 2.5b, восстановлен и PDF/A 2.5,
// подписи 4.6, форма и JavaScript 6.1, скан 8.1. Полоса — ui/widgets/message_bar.h (тон, иконка, кнопки, ×).
#pragma once

#include <QList>
#include <QWidget>

#include "ui/widgets/tone.h"

class QVBoxLayout;

namespace ot::ui {

class MessageBar;

enum class MessageKind {
    ProtectedView,
    SignatureInvalid,
    Repaired,
    JavaScriptOff,
    SignatureWarning,
    SignatureValid,
    PdfA,
    Form,
    LooksScanned,
    ViewOnly,
    Custom,  // любые прочие: каждая показанная — своя полоса, после всех видов
};

class MessageStack : public QWidget {
    Q_OBJECT
public:
    explicit MessageStack(QWidget* parent = nullptr);

    static Tone toneOf(MessageKind kind);
    static int priorityOf(MessageKind kind);  // меньше — выше

    // Показать полосу вида kind (у вида уже есть — та же полоса с новым текстом и без прежних кнопок).
    MessageBar* showMessage(MessageKind kind, const QString& text);
    // Своя полоса с тоном (Custom) — после полос видов.
    MessageBar* add(Tone tone, const QString& text);
    MessageBar* bar(MessageKind kind) const;
    void remove(MessageKind kind);
    void removeBar(MessageBar* bar);
    // Видимые полосы сверху вниз.
    QList<MessageBar*> bars() const;
    static MessageKind kindOf(const MessageBar* bar);

signals:
    void changed();

private:
    MessageBar* insert(MessageKind kind, Tone tone, const QString& text);
    void applyMetrics();

    QVBoxLayout* layout_ = nullptr;
    QList<MessageBar*> bars_;  // по порядку стека
};

}  // namespace ot::ui
