#include "ui/shell/signature_status.h"

#include <QPainter>
#include <QTimer>

#include <exception>

#include "services/crypto_worker.h"
#include "services/settings.h"
#include "services/sign_ops.h"
#include "services/trust_service.h"
#include "ui/shell/document_view.h"
#include "ui/shell/message_bar.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/view/overlay.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"

namespace ot::ui {

// Значки статуса на видимых полях подписи (левый верхний угол рамки).
class SignatureBadges : public Overlay {
public:
    struct Badge {
        int page = -1;
        RectF rect;
        sign::Verdict verdict = sign::Verdict::Unknown;
        bool certification = false;
    };

    void setBadges(std::vector<Badge> badges) { badges_ = std::move(badges); }
    const std::vector<Badge>& badges() const { return badges_; }

    void paintPage(QPainter& painter, const PageView& view, int page) override {
        const int size = Theme::sizes().iconRow;
        for (const Badge& b : badges_) {
            if (b.page != page) {
                continue;
            }
            const QRectF r = view.pageTransform(page).mapRect(
                QRectF(QPointF(b.rect.x0, b.rect.y0), QPointF(b.rect.x1, b.rect.y1)));
            const QRectF at(r.topLeft() + QPointF(2, 2), QSizeF(size, size));
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::colors().surface.color());
            painter.drawEllipse(at.adjusted(-2, -2, 2, 2));
            painter.drawPixmap(at.topLeft(), Icons::get(SignTexts::verdictIcon(b.verdict, b.certification),
                                                        toneIcon(SignTexts::verdictTone(b.verdict)), size));
            painter.restore();
        }
    }

private:
    std::vector<Badge> badges_;
};

SignatureStatus::SignatureStatus(DocumentView& view)
    : view_(view), badges_(std::make_unique<SignatureBadges>()) {
    view_.view().overlays().add(badges_.get(), 45);
    connect(&view_.session(), &DocumentSession::saved, this, &SignatureStatus::onSaved, Qt::QueuedConnection);
    connect(
        &TrustService::shared(), &TrustService::changed, this,
        [this] {
            if (hasResult_ && !verification_.signatures.empty()) {
                verify();
            }
        },
        Qt::QueuedConnection);
    // Проверка при открытии — после того, как окно довело вкладку до вида (защищённый просмотр, полосы).
    if (view_.session().summary().isSigned && Settings().get(settings::kVerifyOnOpen)) {
        QTimer::singleShot(0, this, &SignatureStatus::verify);
    }
}

SignatureStatus::~SignatureStatus() { view_.view().overlays().remove(badges_.get()); }

void SignatureStatus::verify() {
    if (running_) {
        again_ = true;
        return;
    }
    if (view_.isViewOnly() || !view_.session().isOpen()) {
        return;
    }
    running_ = true;
    const quint64 generation = ++generation_;
    emit started();
    QFuture<std::vector<sign::SignatureField>> fields = signing::fields(view_.session(), TaskPriority::Low);
    CryptoWorker::shared()
        .verify(view_.session(), VerifyRequest::fromSettings(Settings()))
        .then(this, [this, generation, fields](QFuture<sign::DocumentVerification> done) mutable {
            sign::DocumentVerification result;
            QString error;
            try {
                result = done.result();
            } catch (const std::exception& e) {
                error = QString::fromUtf8(e.what());
            }
            fields.then(this, [this, generation, result = std::move(result),
                               error](QFuture<std::vector<sign::SignatureField>> f) mutable {
                std::vector<sign::SignatureField> list;
                try {
                    list = f.result();
                } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): поля не прочитаны
                }
                if (generation == generation_) {
                    finish(std::move(result), std::move(list), error);
                }
            });
        });
}

void SignatureStatus::finish(sign::DocumentVerification v, std::vector<sign::SignatureField> fields,
                             QString error) {
    running_ = false;
    verification_ = std::move(v);
    fields_ = std::move(fields);
    error_ = std::move(error);
    hasResult_ = true;
    std::vector<SignatureBadges::Badge> badges;
    for (const sign::SignatureInfo& s : verification_.signatures) {
        if (s.visible && s.page >= 0) {
            badges.push_back({s.page, s.rect, s.verdict, s.kind == sign::SignatureKind::Certification});
        }
    }
    badges_->setBadges(std::move(badges));
    view_.view().viewport()->update();
    showBar();
    emit verified();
    if (again_) {
        again_ = false;
        verify();
    }
}

const sign::SignatureInfo* SignatureStatus::signature(const QString& field) const {
    const std::string name = field.toStdString();
    for (const sign::SignatureInfo& s : verification_.signatures) {
        if (s.field == name) {
            return &s;
        }
    }
    return nullptr;
}

MessageBar* SignatureStatus::bar() const {
    MessageStack& stack = view_.messageStack();
    for (const MessageKind k :
         {MessageKind::SignatureValid, MessageKind::SignatureWarning, MessageKind::SignatureInvalid}) {
        if (MessageBar* b = stack.bar(k)) {
            return b;
        }
    }
    return nullptr;
}

void SignatureStatus::clear() {
    MessageStack& stack = view_.messageStack();
    for (const MessageKind k :
         {MessageKind::SignatureValid, MessageKind::SignatureWarning, MessageKind::SignatureInvalid}) {
        stack.remove(k);
    }
}

void SignatureStatus::showBar() {
    const sign::DocumentStatus status = verification_.status;
    if (status == sign::DocumentStatus::NotSigned) {
        clear();
        return;
    }
    MessageKind kind = MessageKind::SignatureValid;
    QString text = SignTexts::documentStatus(status);
    QString trustField;
    if (status == sign::DocumentStatus::Certified) {
        for (const sign::SignatureInfo& s : verification_.signatures) {
            if (s.kind == sign::SignatureKind::Certification && s.signer) {
                text = tr("Сертифицировано: %1. Подписи действительны.")
                           .arg(QString::fromStdString(s.signer->commonName));
            }
        }
    } else if (status == sign::DocumentStatus::Problems) {
        kind = MessageKind::SignatureWarning;
        if (Settings().get(settings::kVerifyPromptTrust)) {
            for (const sign::SignatureInfo& s : verification_.signatures) {
                if (s.verdict == sign::Verdict::ValidUntrusted && s.intact && trustField.isEmpty()) {
                    trustField = QString::fromStdString(s.field);
                }
            }
        }
    } else if (status == sign::DocumentStatus::Invalid) {
        kind = MessageKind::SignatureInvalid;
    }
    MessageStack& stack = view_.messageStack();
    for (const MessageKind k :
         {MessageKind::SignatureValid, MessageKind::SignatureWarning, MessageKind::SignatureInvalid}) {
        if (k != kind) {
            stack.remove(k);
        }
    }
    MessageBar* bar = stack.showMessage(kind, text);
    bar->setObjectName(QStringLiteral("signatureBar"));
    bar->addAction(tr("Панель подписей"), QStringLiteral("pane"), true);
    if (!trustField.isEmpty()) {
        bar->addAction(tr("Доверять подписанту…"), QStringLiteral("trust"));
    }
    disconnect(bar, &MessageBar::actionTriggered, this, nullptr);
    connect(bar, &MessageBar::actionTriggered, this, [this, trustField](const QString& id) {
        if (id == QLatin1String("pane")) {
            emit paneRequested();
        } else if (id == QLatin1String("trust")) {
            emit trustRequested(trustField);
        }
    });
}

void SignatureStatus::onSaved() {
    // Подпись, метка, сведения LTV или очистка записаны — проверить файл заново (и если подписей больше нет).
    if (view_.session().summary().isSigned || hasResult_) {
        verify();
    }
}

}  // namespace ot::ui
