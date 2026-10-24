// Инструмент «Использовать сертификат» (план 4.6, п. 1; эталон 3.8 «Инструмент», 6.2 п. 11, 2 — сценарий 4;
// sign-12, sign-16, sign-20, sign-08): панель слева — «Поставить цифровую подпись», «Сертифицировать (видимая
// подпись)», «Сертифицировать (невидимая подпись)», «Метка времени документа», «Проверить все подписи»,
// «Удостоверения…»; те же команды — в меню «Подпись». Поток подписи: проверка, можно ли (не PDF, защищённый
// просмотр, только чтение, права /P «заполнение форм и подпись», DocMDP P=1; сертификация — только документ
// без подписей) → рамка на странице (SignPlacementTool: протяжка или щелчок по пустому полю /Sig; невидимая —
// без рамки) → диалог (SignDialog / CertifyDialog) → «Сохранить как» (файл документа — дописать в него) →
// DocumentSession::sign: инкрементальная запись, метка B-T службой из «Серверов времени», B-LT/B-LTA —
// сведения для проверки и метка документа; служба времени недоступна — по настройке спросить «подписать без
// метки?» (вопрос из потока документа — в окне), подписать без метки или отменить. Итог — тост; проверку
// записанного файла и полосу делает проверка подписей вкладки (4.6, п. 2); B-B/B-T — сведения LTV по
// настройке (спросить полосой, добавить сразу, не добавлять). Метка времени документа — TimestampDialog →
// «Сохранить как» → DocumentSession::timestampDocument.
#pragma once

#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "core/sign/docmdp.h"
#include "core/sign/sig_fields.h"
#include "core/util/geometry.h"
#include "ui/tools/tool.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/tool_button.h"

class QDialog;
class QVBoxLayout;

namespace ot::sign {
class TrustStore;
}

namespace ot::ui {

class DocumentView;
class PageView;
class SignDialog;
class SignPlacementTool;
class TimestampDialog;
class ToolRegistry;

class CertificateTool : public QObject, public Tool {
    Q_OBJECT
public:
    enum class Action { Sign, CertifyVisible, CertifyInvisible, Timestamp };
    // «Сохранить как» подписанного документа: путь (пусто — отмена); suggested — текущий файл.
    using FileChooser = std::function<QString(QWidget* parent, const QString& suggested)>;
    // Служба времени не ответила: подписать без метки? (вопрос в окне; message — причина).
    using TimestampPrompt = std::function<bool(const QString& message)>;

    explicit CertificateTool(ToolRegistry& registry);
    ~CertificateTool() override;

    QString id() const override { return QStringLiteral("certificate"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 4; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;
    std::vector<ot::Command> commands() const override;

    // Инструмент вида «рамка подписи» (ставится при первом обращении).
    static SignPlacementTool& placementOf(PageView& view);

    // Почему действие сейчас нельзя (пусто — можно); вкладка — текущая инструмента или окна.
    QString blockReason(Action action) const;
    DocumentView* view() const;

    // Поток (тесты и команды): false — нельзя (причина — тостом).
    bool startSign();
    bool startCertify(bool visible);
    bool startTimestamp();
    // Подписать в рамке без мыши (тесты, --script): как будто рамку нарисовали.
    void placeSignature(int page, const RectF& rect, const QString& field = {});
    // «Проверить все подписи» (4.6, п. 2 — проверка вкладки и панель «Подписи»).
    void verifyAll();
    // Установки → Подписи (удостоверения).
    void showIdentities();

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    void setTimestampPrompt(TimestampPrompt prompt) { timestampPrompt_ = std::move(prompt); }

    // Открытый диалог потока (подписание, сертификация, метка времени); nullptr — нет.
    QDialog* dialog() const { return dialog_; }
    bool busy() const { return busy_; }
    MessageBar* placementBar() const { return placementBar_; }
    QLabel* lockLabel() const { return lockLabel_; }
    ToolButton* button(Action action) const;

signals:
    void dialogOpened(QDialog* dialog);
    // Сведения LTV после подписи добавлены (true) или нет.
    void ltvFinished(bool added);
    // Подпись или метка записаны (ok) или нет (сообщение — по-русски).
    void finished(bool ok, const QString& message);
    void lockChanged();

private:
    struct Pending {
        Action action = Action::Sign;
        int page = 0;
        RectF rect;
        QString field;
    };

    void buildPanel();
    void syncButtons();
    void refreshDocState();
    bool begin(Action action);
    void armPlacement(Action action);
    void endPlacement();
    void openDialog(const Pending& pending);
    void signWith(SignDialog& dialog, const Pending& pending);
    void timestampWith(TimestampDialog& dialog);
    QString chooseTarget(DocumentView& view);
    bool askWithoutTimestamp(const QString& message);
    void offerLtv(DocumentView& view);
    void askLtv(DocumentView& view, const std::function<void()>& add);
    void done(bool ok, const QString& message);
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, Action action);
    void addCaption(QVBoxLayout* column, const QString& text);

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    QPointer<QLabel> lockLabel_;
    QList<QPointer<ToolButton>> buttons_;
    QList<QPointer<QLabel>> captions_;
    QPointer<ToolButton> verifyButton_;
    QPointer<ToolButton> identitiesButton_;
    QPointer<QDialog> dialog_;
    QPointer<MessageBar> placementBar_;
    std::optional<Pending> placing_;
    FileChooser chooser_;
    TimestampPrompt timestampPrompt_;
    bool busy_ = false;
    // Состояние документа вкладки (задачей): DocMDP и подписи.
    sign::MdpPermission mdp_ = sign::MdpPermission::None;
    bool hasSignatures_ = false;
    quint64 stateRequest_ = 0;
    std::shared_ptr<const sign::TrustStore> trust_;              // снимок для B-LT/B-LTA на время подписи
    std::vector<QMetaObject::Connection> connections_;           // с вкладкой
    std::vector<QMetaObject::Connection> placementConnections_;  // с рамкой подписи
};

}  // namespace ot::ui
