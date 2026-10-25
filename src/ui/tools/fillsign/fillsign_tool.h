// Инструмент «Заполнить и подписать» из «Все инструменты» (план 5.3; эталон 3.5, 6.2 п. 4, 6.3 «Fill & Sign»;
// forms-07, sign-01, sign-02, comment-09): панель слева 320 — «Добавить текст» и отметки ✓ ✕ ● — ▭, «Подпись
// ▾» и «Инициалы ▾» (сохранённая картинка — поставить, крестик — удалить, «Добавить…» —
// SignatureCaptureDialog), стиль — кегль и цвет (новому тексту и отметкам, а выбранному — сразу: тексту —
// цвет и кегль, подписи — цвет картинки), «Сохранить как → Плоская копия…»; режим вида FillSignMode в слоте
// ViewToolId::FillSign. Документ с полями AcroForm — подсказка «Заполнить форму» (6.1), инструмент работает
// поверх плоских областей; подписанный — предупреждение: подпись остаётся действительной, Reader покажет
// изменения после подписания (запись — инкрементом). Заполнять нельзя (защищённый просмотр, только чтение, не
// PDF, право /P) — кнопки серые, причина — строкой панели. Fill & Sign-подпись — картинка, не
// криптографическая подпись.
#pragma once

#include <QLabel>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QString>

#include <map>
#include <vector>

#include "services/saved_signatures.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/tools/tool.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/tool_button.h"

class QVBoxLayout;

namespace ot::ui {

class PageView;
class SignatureCaptureDialog;
class ToolRegistry;

class FillSignTool : public QObject, public Tool {
    Q_OBJECT
public:
    explicit FillSignTool(ToolRegistry& registry);
    ~FillSignTool() override;

    QString id() const override { return QStringLiteral("fillsign"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 5; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;

    // Режим вида (ставится в слот при первом обращении).
    static FillSignMode& modeOf(PageView& view);
    FillSignMode* mode() const;
    DocumentView* view() const;
    QString lockReason() const { return lock_; }

    // Включить действие на вкладке инструмента: режим вида + кнопка панели.
    void start(FillSignAction action);
    ToolButton* actionButton(FillSignAction action) const;
    QLabel* lockLabel() const { return lockLabel_; }
    QLabel* formHint() const { return formHint_; }
    QPushButton* fillFormButton() const { return fillForm_; }
    QLabel* signedNote() const { return signedNote_; }
    NumberField* fontSizeField() const { return fontSize_; }
    ColorSwatch* colorSwatch() const { return color_; }
    ToolButton* flattenButton() const { return flatten_; }

    // «Подпись ▾» / «Инициалы ▾»: меню (пересобирается при показе), поставить сохранённую, добавить новую
    // (диалог — окно-модальный; «Применить» сразу включает постановку), удалить сохранённую.
    QMenu* signatureMenu(fillsign::SignatureKind kind);
    void useSaved(fillsign::SignatureKind kind);
    SignatureCaptureDialog* addSignature(fillsign::SignatureKind kind);
    bool removeSaved(fillsign::SignatureKind kind);
    // «Сохранить как → Плоская копия…» (SaveFlow::saveFlatCopy).
    void saveFlatCopy();

signals:
    void lockChanged(const QString& reason);

private:
    void buildPanel();
    void updateLock();
    void updateNotes();
    void syncButtons();
    void fillSignatureMenu(fillsign::SignatureKind kind);
    void styleChanged();
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, const QString& label);
    void addCaption(QVBoxLayout* column, const QString& text);

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    std::map<FillSignAction, QPointer<ToolButton>> buttons_;
    std::map<fillsign::SignatureKind, QPointer<QMenu>> menus_;
    QPointer<QLabel> lockLabel_;
    QPointer<QLabel> formHint_;
    QPointer<QPushButton> fillForm_;
    QPointer<QLabel> signedNote_;
    QPointer<NumberField> fontSize_;
    QPointer<ColorSwatch> color_;
    QPointer<ToolButton> flatten_;
    QString lock_;
    bool loading_ = false;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace ot::ui
