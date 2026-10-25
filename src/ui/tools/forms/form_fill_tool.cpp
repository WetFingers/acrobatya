#include "ui/tools/forms/form_fill_tool.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolTip>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "core/links/links.h"
#include "services/document_session.h"
#include "services/forms_ops.h"
#include "services/http_fetcher.h"
#include "services/mail.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/dialogs/form_data_dialog.h"
#include "ui/dialogs/submit_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/message_bar.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/tools/forms/field_editors.h"
#include "ui/tools/forms/js_alert_dialog.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

namespace {

constexpr int kOverlayZ = 8;  // под совпадениями поиска (10) и выделением (20)

bool textLike(forms::FieldType t) {
    return t == forms::FieldType::Text || t == forms::FieldType::ComboBox || t == forms::FieldType::ListBox;
}

QRectF viewRect(const PageView& view, int page, const RectF& r) {
    return view.pageTransform(page).mapRect(QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)));
}

QColor settingColor(const QString& value, const QColor& fallback) {
    const QColor c(value);
    return value.isEmpty() || !c.isValid() ? fallback : c;
}

}  // namespace

FormFillTool::FormFillTool(DocumentView& view)
    : view_(view), page_(view.view()), prompter_(std::make_shared<JsPrompter>()) {
    const Settings settings;
    highlight_ = settings.get(settings::kFormHighlight);
    focusRect_ = settings.get(settings::kFormFocusRect);
    page_.overlays().add(this, kOverlayZ);
    page_.setFieldInteraction(this);
    setAlertPrompt({});
    connect(prompter_.get(), &JsPrompter::launchRequested, this, [this](const QString& url) {
        links::LinkAction action;
        action.kind = links::LinkKind::Uri;
        action.uri = url.toStdString();
        emit page_.linkActivated(action);  // окно спросит, как у ссылки (эталон 7.4)
    });
    connect(&view_.session(), &DocumentSession::documentChanged, this, &FormFillTool::reload,
            Qt::QueuedConnection);
    connect(&view_.session(), &DocumentSession::saved, this, &FormFillTool::reload, Qt::QueuedConnection);
    connect(&view_, &DocumentView::protectedViewChanged, this, [this] {
        jsDecided_ = false;
        if (loaded_) {
            decideJavaScript();
        }
    });
    reload();
}

FormFillTool::~FormFillTool() {
    prompter_->setPrompt({});  // мост сессии может пережить вкладку — окна без неё не показывать
    if (editor_ != nullptr) {
        editor_->finish();
        delete editor_;
    }
    page_.setFieldInteraction(nullptr);
    page_.overlays().remove(this);
}

void FormFillTool::setAlertPrompt(JsPrompter::Prompt prompt) {
    if (!prompt) {
        QPointer<DocumentView> owner = &view_;
        prompt = [owner](const forms::AlertRequest& request) {
            return JsAlertDialog::ask(owner != nullptr ? owner->window() : nullptr, request);
        };
    }
    prompter_->setPrompt(std::move(prompt));
}

void FormFillTool::reload() {
    const quint64 generation = ++generation_;
    forms::snapshot(view_.session()).then(this, [this, generation](QFuture<forms::FormSnapshot> done) {
        if (generation != generation_) {
            return;
        }
        try {
            forms::FormSnapshot s = done.result();
            applySnapshot(std::move(s.form), std::move(s.tabs));
        } catch (const std::exception& e) {
            spdlog::warn("forms: snapshot failed: {}", e.what());
        }
    });
}

void FormFillTool::applySnapshot(forms::Form form, std::map<int, std::vector<int>> tabs) {
    form_ = std::move(form);
    tabs_ = std::move(tabs);
    loaded_ = true;
    if (focus_ && find(focus_->page, focus_->objnum).field == nullptr) {
        focus_.reset();
    }
    if (editor_ != nullptr && find(editor_->page(), editor_->objnum()).field == nullptr) {
        cancelEditor();
    }
    updateBars();
    if (!jsDecided_) {
        decideJavaScript();
    }
    repaint();
    emit loaded();
}

// ---------------------------------------------------------------- полосы и JavaScript

void FormFillTool::updateBars() {
    MessageStack& stack = view_.messageStack();
    if (hasForm() && Settings().get(settings::kFormShowBar)) {
        MessageBar* bar = stack.bar(MessageKind::Form);
        if (bar == nullptr) {
            bar = stack.showMessage(MessageKind::Form,
                                    tr("Документ содержит поля формы, которые можно заполнить."));
            bar->setObjectName(QStringLiteral("formBar"));
            QPushButton* button = bar->addAction(QString(), QStringLiteral("highlight"), true);
            button->setObjectName(QStringLiteral("formHighlightButton"));
            connect(bar, &MessageBar::actionTriggered, this, [this](const QString& id) {
                if (id == QLatin1String("highlight")) {
                    setHighlight(!highlight_);
                }
            });
        }
        if (auto* button = bar->findChild<QPushButton*>(QStringLiteral("formHighlightButton"))) {
            button->setText(highlight_ ? tr("Убрать подсветку") : tr("Подсветить поля"));
        }
    } else {
        stack.remove(MessageKind::Form);
    }
    if (form_.xfa) {
        if (stack.bar(MessageKind::Xfa) == nullptr) {
            MessageBar* bar = stack.showMessage(
                MessageKind::Xfa, tr("Формы XFA не поддерживаются: показана часть формы AcroForm. Поля, "
                                     "которых в ней нет, заполнить нельзя."));
            bar->setObjectName(QStringLiteral("xfaBar"));
        }
    } else {
        stack.remove(MessageKind::Xfa);
    }
    if (jsState_ == forms::JsState::Disabled) {
        if (stack.bar(MessageKind::JavaScriptOff) == nullptr) {
            MessageBar* bar = stack.showMessage(MessageKind::JavaScriptOff,
                                                tr("JavaScript в этом документе отключён: вычисления, "
                                                   "форматы и проверки полей не выполняются."));
            bar->setObjectName(QStringLiteral("jsBar"));
            bar->addAction(tr("Включить для этого файла"), QStringLiteral("enableJs"), true);
            connect(bar, &MessageBar::actionTriggered, this, [this](const QString& id) {
                if (id == QLatin1String("enableJs")) {
                    enableJavaScriptForFile();
                }
            });
        }
    } else {
        stack.remove(MessageKind::JavaScriptOff);
    }
}

void FormFillTool::decideJavaScript() {
    jsDecided_ = true;
    DocumentSession& session = view_.session();
    if (!session.summary().isPdf) {
        return;
    }
    const forms::JsState state = js_policy::decide(view_.path(), view_.isProtectedView(), form_.javaScript);
    jsState_ = state;
    if (forms::enabledBy(state)) {
        if (!session.javaScriptEnabled()) {
            session.setJavaScript(prompter_);
        }
    } else if (session.javaScriptEnabled()) {
        session.setJavaScript(nullptr);
    }
    spdlog::info("forms: JavaScript {}", forms::jsStateName(state));
    updateBars();
    emit javaScriptChanged(state);
}

void FormFillTool::enableJavaScriptForFile() {
    const QString path = view_.path();
    if (!path.isEmpty() && !js_policy::enableForFile(path)) {
        spdlog::warn("forms: cannot remember JavaScript for {}", path.toStdString());
    }
    jsState_ = forms::JsState::Enabled;
    view_.session().setJavaScript(prompter_);
    updateBars();
    emit javaScriptChanged(jsState_);
}

// ---------------------------------------------------------------- подсветка и настройки

void FormFillTool::setHighlight(bool on) {
    highlight_ = on;
    Settings().set(settings::kFormHighlight, on);
    updateBars();
    repaint();
}

void FormFillTool::settingsChanged() {
    const Settings settings;
    highlight_ = settings.get(settings::kFormHighlight);
    focusRect_ = settings.get(settings::kFormFocusRect);
    updateBars();
    repaint();
}

void FormFillTool::repaint() { page_.viewport()->update(); }

void FormFillTool::paintPage(QPainter& painter, const PageView& view, int page) {
    if (form_.fields.empty()) {
        return;
    }
    const Settings settings;
    const auto& colors = Theme::colors();
    // Подсветка лежит на бумаге документа (светлой в любой теме) — цвет светлой палитры.
    QColor fill = settingColor(settings.get(settings::kFormFieldColor), kLightPalette.formField.color());
    fill.setAlphaF(0.6);
    QColor muted = colors.textMuted.color();
    muted.setAlphaF(0.18);
    const QColor required = settingColor(settings.get(settings::kFormRequiredColor), colors.danger.color());
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const forms::Field& f : form_.fields) {
        if (f.type == forms::FieldType::Unknown) {
            continue;
        }
        const bool rejected = rejected_.count(QString::fromStdString(f.name)) != 0;
        for (const forms::Widget& w : f.widgets) {
            if (w.page != page || w.hidden) {
                continue;
            }
            const QRectF r = viewRect(view, page, w.rect);
            if (highlight_) {
                painter.fillRect(r, f.readOnly() ? muted : fill);
                if (f.required() && !f.readOnly()) {
                    painter.setPen(QPen(required, 1.5));
                    painter.drawRect(r.adjusted(0.75, 0.75, -0.75, -0.75));
                }
            }
            if (rejected) {
                painter.setPen(QPen(colors.danger.color(), 2));
                painter.drawRect(r.adjusted(1, 1, -1, -1));
            }
            if (focusRect_ && focus_ && focus_->page == page && focus_->objnum == w.objnum &&
                editor_ == nullptr) {
                QPen pen(colors.accent.color(), 1.5, Qt::DashLine);
                painter.setPen(pen);
                painter.drawRect(r.adjusted(-2, -2, 2, 2));
            }
        }
    }
    painter.restore();
}

// ---------------------------------------------------------------- поиск поля

FormFillTool::Hit FormFillTool::find(int page, int objnum) const {
    for (const forms::Field& f : form_.fields) {
        for (const forms::Widget& w : f.widgets) {
            if (w.page == page && w.objnum == objnum) {
                return {&f, &w};
            }
        }
    }
    return {};
}

FormFillTool::Hit FormFillTool::hitAt(QPointF viewportPoint) const {
    if (form_.fields.empty()) {
        return {};
    }
    const int page = page_.pageAtViewport(viewportPoint);
    if (page < 0) {
        return {};
    }
    const QPointF p = page_.viewportToPage(page, viewportPoint);
    Hit hit;
    for (const forms::Field& f : form_.fields) {
        for (const forms::Widget& w : f.widgets) {
            if (w.page == page && !w.hidden && p.x() >= w.rect.x0 && p.x() <= w.rect.x1 &&
                p.y() >= w.rect.y0 && p.y() <= w.rect.y1) {
                hit = {&f, &w};
            }
        }
    }
    return hit;
}

bool FormFillTool::editable() const {
    return !view_.isViewOnly() && !view_.isReadOnly() && view_.permissions().fillForms;
}

bool FormFillTool::interactive(const forms::Field& f) const {
    if (f.type == forms::FieldType::Signature) {
        return !f.isSigned && view_.canEdit();
    }
    if (f.type == forms::FieldType::PushButton) {
        return true;  // кнопки работают и в форме только для чтения (сброс, переход, отправка)
    }
    return editable() && !f.readOnly() && f.type != forms::FieldType::Unknown;
}

FieldEditor* FormFillTool::editor() const noexcept { return editor_; }

QString FormFillTool::focusedField() const {
    if (!focus_) {
        return {};
    }
    const Hit h = find(focus_->page, focus_->objnum);
    return h.field != nullptr ? QString::fromStdString(h.field->name) : QString();
}

// ---------------------------------------------------------------- мышь и клавиши

bool FormFillTool::fieldPress(QMouseEvent* event) {
    const Hit h = hitAt(event->position());
    if (h.field == nullptr || !interactive(*h.field)) {
        return false;
    }
    pressed_.reset();
    if (textLike(h.field->type)) {
        setFocus(Focus{h.widget->page, h.widget->objnum});
        openEditor(*h.field, *h.widget);
        if (h.field->type == forms::FieldType::ComboBox && editor_ != nullptr && !h.field->editable()) {
            popupOnRelease_ =
                true;  // список — после отпускания: иначе отпускание выбрало бы строку под мышью
        }
        return true;
    }
    if (editor_ != nullptr) {
        commitEditor(0);
    }
    setFocus(Focus{h.widget->page, h.widget->objnum});
    pressed_ = Focus{h.widget->page, h.widget->objnum};
    return true;
}

bool FormFillTool::fieldRelease(QMouseEvent* event) {
    if (std::exchange(popupOnRelease_, false) && editor_ != nullptr) {
        editor_->openPopup();
        return true;
    }
    const std::optional<Focus> pressed = std::exchange(pressed_, std::nullopt);
    if (!pressed) {
        return false;
    }
    const Hit h = hitAt(event->position());
    if (h.widget != nullptr && h.widget->page == pressed->page && h.widget->objnum == pressed->objnum) {
        press(*h.field, *h.widget);
    }
    return true;
}

std::optional<QCursor> FormFillTool::fieldCursor(QPointF point) const {
    const Hit h = hitAt(point);
    if (h.field == nullptr || !interactive(*h.field)) {
        return std::nullopt;
    }
    if (h.field->type == forms::FieldType::Text ||
        (h.field->type == forms::FieldType::ComboBox && h.field->editable())) {
        return QCursor(Qt::IBeamCursor);
    }
    return QCursor(Qt::PointingHandCursor);
}

bool FormFillTool::fieldKey(QKeyEvent* event) {
    if (!focus_ || editor_ != nullptr) {
        return false;
    }
    const Hit h = find(focus_->page, focus_->objnum);
    if (h.field == nullptr) {
        return false;
    }
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        setFocus(std::nullopt);
        return true;
    }
    if (!interactive(*h.field)) {
        return false;
    }
    const forms::FieldType type = h.field->type;
    if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (textLike(type)) {
            openEditor(*h.field, *h.widget);
        } else {
            press(*h.field, *h.widget);
        }
        return true;
    }
    if (type == forms::FieldType::RadioButton &&
        (key == Qt::Key_Down || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Left)) {
        // Стрелки в группе: следующий/предыдущий переключатель выбирается и получает фокус.
        const std::vector<forms::Widget>& ws = h.field->widgets;
        const auto it = std::find_if(ws.begin(), ws.end(), [&](const forms::Widget& w) {
            return w.page == focus_->page && w.objnum == focus_->objnum;
        });
        if (it == ws.end() || ws.size() < 2) {
            return true;
        }
        const bool forward = key == Qt::Key_Down || key == Qt::Key_Right;
        std::size_t i = static_cast<std::size_t>(it - ws.begin());
        i = forward ? (i + 1) % ws.size() : (i + ws.size() - 1) % ws.size();
        const forms::Widget target = ws[i];
        setFocus(Focus{target.page, target.objnum});
        // Снимок мог ещё не прийти после прошлого щелчка — значение ставится всегда (то же — без шага).
        const QString name = QString::fromStdString(h.field->name);
        forms::setValue(view_.session(), name, QString::fromStdString(target.onState),
                        Settings().get(settings::kFormAutoCalculate))
            .then(this, [this, name](QFuture<forms::SetResult> done) {
                try {
                    afterWrite(name, done.result());
                } catch (const std::exception& e) {
                    spdlog::warn("forms: radio failed: {}", e.what());
                }
            });
        page_.ensureVisible(target.page, target.rect, 16);
        return true;
    }
    return false;
}

bool FormFillTool::wantsKey(const QKeyEvent* event) const {
    if (!focus_ || editor_ != nullptr || (event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier) {
        return false;
    }
    const Hit h = find(focus_->page, focus_->objnum);
    if (h.field == nullptr || !interactive(*h.field)) {
        return false;
    }
    const int key = event->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        return true;
    }
    return h.field->type == forms::FieldType::RadioButton &&
           (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right);
}

bool FormFillTool::focusNextField(bool next) {
    if (!hasForm() || !editable()) {
        return false;
    }
    const std::optional<Focus> to = nextStop(focus_, next);
    if (!to) {
        setFocus(std::nullopt);
        return false;
    }
    setFocus(to);
    const Hit h = find(to->page, to->objnum);
    page_.ensureVisible(to->page, h.widget->rect, 16);
    if (textLike(h.field->type) && interactive(*h.field)) {
        openEditor(*h.field, *h.widget);
    }
    return true;
}

std::optional<FormFillTool::Focus> FormFillTool::nextStop(std::optional<Focus> from, bool forward) const {
    std::vector<Focus> stops;
    for (const auto& [page, objnums] : tabs_) {
        for (const int objnum : objnums) {
            stops.push_back(Focus{page, objnum});
        }
    }
    if (stops.empty()) {
        return std::nullopt;
    }
    if (!from) {
        return forward ? stops.front() : stops.back();
    }
    auto it = std::find(stops.begin(), stops.end(), *from);
    if (it == stops.end()) {
        // Не остановка (переключатель группы, только для чтения): ближайшая следующая по странице.
        const Hit h = find(from->page, from->objnum);
        if (h.field != nullptr) {
            it = std::find_if(stops.begin(), stops.end(), [&](const Focus& s) {
                const Hit o = find(s.page, s.objnum);
                return o.field == h.field;
            });
        }
        if (it == stops.end()) {
            return forward ? stops.front() : stops.back();
        }
    }
    if (forward) {
        ++it;
        return it == stops.end() ? std::nullopt : std::optional<Focus>(*it);
    }
    if (it == stops.begin()) {
        return std::nullopt;
    }
    return *(--it);
}

void FormFillTool::moveFocus(bool forward) {
    if (!focusNextField(forward)) {
        page_.setFocus(Qt::OtherFocusReason);
    }
}

void FormFillTool::setFocus(std::optional<Focus> focus) {
    if (focus_ == focus) {
        return;
    }
    focus_ = focus;
    repaint();
    emit focusChanged();
}

bool FormFillTool::focusField(const QString& name) {
    const forms::Field* f = form_.find(name.toStdString());
    if (f == nullptr || f->widgets.empty()) {
        return false;
    }
    const forms::Widget& w = f->widgets.front();
    setFocus(Focus{w.page, w.objnum});
    page_.ensureVisible(w.page, w.rect, 16);
    if (textLike(f->type) && interactive(*f)) {
        openEditor(*f, w);
    }
    return true;
}

bool FormFillTool::activate(int page, int objnum) {
    const Hit h = find(page, objnum);
    if (h.field == nullptr || !interactive(*h.field)) {
        return false;
    }
    setFocus(Focus{page, objnum});
    if (textLike(h.field->type)) {
        openEditor(*h.field, *h.widget);
    } else {
        press(*h.field, *h.widget);
    }
    return true;
}

// ---------------------------------------------------------------- редактор

void FormFillTool::openEditor(const forms::Field& field, const forms::Widget& widget) {
    if (editor_ != nullptr) {
        if (editor_->page() == widget.page && editor_->objnum() == widget.objnum) {
            return;
        }
        commitEditor(0);
    }
    editor_ = new FieldEditor(page_, field, widget, QString::fromStdString(field.value));
    connect(editor_, &FieldEditor::commitRequested, this, &FormFillTool::commitEditor);
    connect(editor_, &FieldEditor::cancelRequested, this, &FormFillTool::cancelEditor);
    if (view_.session().javaScriptEnabled()) {
        forms::focus(view_.session(), widget.page, widget.objnum, true);
    }
    repaint();
}

void FormFillTool::closeEditor() {
    if (editor_ == nullptr) {
        return;
    }
    FieldEditor* e = editor_;
    editor_ = nullptr;
    e->finish();
    e->hide();
    e->deleteLater();
    if (view_.session().javaScriptEnabled()) {
        forms::focus(view_.session(), e->page(), e->objnum(), false);
    }
    repaint();
}

void FormFillTool::commitEditor(int move) {
    if (editor_ == nullptr || editor_->finished()) {
        return;
    }
    FieldEditor* e = editor_;
    const QString name = e->fieldName();
    const bool autoCalc = Settings().get(settings::kFormAutoCalculate);
    if (e->changed()) {
        QFuture<forms::SetResult> write =
            e->kind() == FieldEditor::Kind::List
                ? forms::setValues(view_.session(), name, e->selected(), autoCalc)
                : forms::setValue(view_.session(), name, e->text(), autoCalc);
        write.then(this, [this, name](QFuture<forms::SetResult> done) {
            try {
                afterWrite(name, done.result());
            } catch (const std::exception& ex) {
                spdlog::warn("forms: write {} failed: {}", name.toStdString(), ex.what());
                emit committed(name, false);
            }
        });
    }
    const Focus from{e->page(), e->objnum()};
    const bool hadFocus = e->isAncestorOf(QApplication::focusWidget());
    closeEditor();
    if (move != 0) {
        focus_ = from;
        moveFocus(move > 0);
    } else if (hadFocus) {
        page_.setFocus(Qt::OtherFocusReason);
    }
}

void FormFillTool::cancelEditor() {
    const bool hadFocus = editor_ != nullptr && editor_->isAncestorOf(QApplication::focusWidget());
    closeEditor();
    if (hadFocus) {
        page_.setFocus(Qt::OtherFocusReason);
    }
}

void FormFillTool::afterWrite(const QString& field, const forms::SetResult& result) {
    if (result.accepted) {
        rejected_.erase(field);
    } else if (result.error == "rejected" || result.error == "not-an-option") {
        rejected_.insert(field);
        // Рамка err и подсказка (эталон 3.6 «недопустимое значение»).
        const forms::Field* f = form_.find(field.toStdString());
        if (f != nullptr && !f->widgets.empty()) {
            const QRectF r = viewRect(page_, f->widgets.front().page, f->widgets.front().rect);
            QToolTip::showText(page_.viewport()->mapToGlobal(r.bottomLeft().toPoint()),
                               tr("Значение не принято: поле его не допускает."), page_.viewport());
        }
    }
    repaint();
    emit committed(field, result.accepted);
}

// ---------------------------------------------------------------- кнопки, флажки, подписи

void FormFillTool::press(const forms::Field& field, const forms::Widget& widget) {
    DocumentSession& session = view_.session();
    const QString name = QString::fromStdString(field.name);
    switch (field.type) {
        case forms::FieldType::CheckBox:
        case forms::FieldType::RadioButton:
            forms::toggle(session, widget.page, widget.objnum, Settings().get(settings::kFormAutoCalculate))
                .then(this, [this, name](QFuture<forms::SetResult> done) {
                    try {
                        afterWrite(name, done.result());
                    } catch (const std::exception& e) {
                        spdlog::warn("forms: toggle failed: {}", e.what());
                    }
                });
            break;
        case forms::FieldType::PushButton:
            forms::click(session, widget.page, widget.objnum)
                .then(this, [this](QFuture<forms::ClickResult> done) {
                    try {
                        handleAction(done.result().action);
                    } catch (const std::exception& e) {
                        spdlog::warn("forms: button failed: {}", e.what());
                    }
                });
            break;
        case forms::FieldType::Signature:
            emit signatureFieldClicked(name);
            break;
        default:
            break;
    }
}

void FormFillTool::handleAction(const forms::FieldAction& action) {
    switch (action.kind) {
        case forms::ActionKind::SubmitForm:
            emit submitRequested(action);
            submit(action);
            break;
        case forms::ActionKind::ImportData: {
            emit importRequested(action);
            // Путь действия — от папки документа (как у Acrobat); импорт спросит подтверждение.
            const QString target = QString::fromStdString(action.target);
            const QString dir = QFileInfo(view_.path()).absolutePath();
            importDataFrom(QFileInfo(target).isAbsolute() ? target : QDir(dir).filePath(target));
            break;
        }
        case forms::ActionKind::Uri:
        case forms::ActionKind::GoToR:
        case forms::ActionKind::Launch: {
            links::LinkAction link;
            link.kind = action.kind == forms::ActionKind::Uri     ? links::LinkKind::Uri
                        : action.kind == forms::ActionKind::GoToR ? links::LinkKind::GoToR
                                                                  : links::LinkKind::Launch;
            link.uri = action.kind == forms::ActionKind::Uri ? action.target : std::string();
            link.file = action.kind == forms::ActionKind::Uri ? std::string() : action.target;
            emit page_.linkActivated(link);
            break;
        }
        case forms::ActionKind::GoTo:
            if (action.page >= 0 && action.page < page_.pageCount()) {
                page_.goToPage(action.page);
            }
            break;
        case forms::ActionKind::Named:
            if (action.target == "NextPage") {
                page_.nextPage();
            } else if (action.target == "PrevPage") {
                page_.previousPage();
            } else if (action.target == "FirstPage") {
                page_.firstPage();
            } else if (action.target == "LastPage") {
                page_.lastPage();
            }
            break;
        default:
            break;  // ResetForm и JavaScript выполнило ядро
    }
}

void FormFillTool::resetForm() {
    cancelEditor();
    rejected_.clear();
    forms::reset(view_.session());
}

// ---------------------------------------------------------------- данные формы

void FormFillTool::exportData() {
    int values = 0;
    for (const forms::Field& f : form_.fields) {
        if (f.type != forms::FieldType::PushButton && f.type != forms::FieldType::Signature &&
            !f.noExport() && !f.password()) {
            ++values;
        }
    }
    auto* dialog = new FormDataExportDialog(view_.path(), values, view_.window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this,
            [this, dialog] { exportDataTo(dialog->path(), dialog->format()); });
    dialog->open();
}

void FormFillTool::exportDataTo(const QString& path, forms::DataFormat format) {
    const QString name = QFileInfo(view_.path()).fileName();
    forms::exportData(view_.session(), format, name).then(this, [this, path](QFuture<QByteArray> done) {
        bool ok = false;
        try {
            const QByteArray bytes = done.result();
            QFile out(path);
            ok = out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size();
        } catch (const std::exception& e) {
            spdlog::warn("forms: export failed: {}", e.what());
        }
        Toast::notify(view_.window(),
                      ok ? tr("Данные формы сохранены: %1").arg(QFileInfo(path).fileName())
                         : tr("Не удалось сохранить данные формы в «%1».").arg(QFileInfo(path).fileName()),
                      ok ? Tone::Ok : Tone::Danger);
        emit dataExported(path, ok);
    });
}

void FormFillTool::importData() {
    QString path;
    if (openChooser_) {
        path = openChooser_();
    } else {
        const QString filter = tr("Данные формы (*.fdf *.xfdf *.xml *.txt *.tsv)");
        path = QFileDialog::getOpenFileName(view_.window(), tr("Импорт данных формы"),
                                            QFileInfo(view_.path()).absolutePath(), filter);
    }
    if (!path.isEmpty()) {
        importDataFrom(path);
    }
}

void FormFillTool::importDataFrom(const QString& path) {
    const QString file = QFileInfo(path).fileName();
    const auto fail = [this, path](const QString& reason) {
        Toast::notify(view_.window(), reason, Tone::Danger);
        emit dataImportFailed(path, reason);
    };
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        fail(tr("Не удалось открыть «%1».").arg(file));
        return;
    }
    const QByteArray raw = in.readAll();
    const std::string bytes(raw.constData(), static_cast<std::size_t>(raw.size()));
    // Формат — по содержимому (расширение может лгать: .txt с XFDF внутри).
    const forms::DataFormat format = forms::detectDataFormat(bytes);
    forms::FormData data;
    try {
        data = forms::readData(bytes, format);
    } catch (const forms::DataError& e) {
        spdlog::warn("forms: {} is not form data: {}", path.toStdString(), e.what());
        fail(tr("«%1» — не файл данных формы или повреждён.").arg(file));
        return;
    }
    const int values = static_cast<int>(std::max(data.fields.size(), data.pairs.size()));
    const bool yes =
        importConfirm_ ? importConfirm_(path, values) : confirmFormDataImport(view_.window(), path, values);
    if (!yes) {
        return;
    }
    forms::importData(view_.session(), std::move(data), QFileInfo(view_.path()).fileName())
        .then(this, [this, path, file, fail](QFuture<forms::DataImportReport> done) {
            forms::DataImportReport report;
            try {
                report = done.result();
            } catch (const forms::DataError& e) {
                if (e.kind() == forms::DataError::Kind::ForeignDocument) {
                    fail(tr("Данные в «%1» относятся к другому документу: %2.")
                             .arg(file, QString::fromStdString(e.file())));
                } else {
                    fail(tr("«%1» — не файл данных формы или повреждён.").arg(file));
                }
                return;
            } catch (const std::exception& e) {
                spdlog::warn("forms: import failed: {}", e.what());
                fail(tr("Не удалось импортировать данные из «%1».").arg(file));
                return;
            }
            const bool clean = report.unknown.empty() && report.readOnly.empty() && report.rejected.empty();
            if (clean || !reportWindow_) {
                Toast::notify(view_.window(),
                              tr("Импортировано значений: %1").arg(static_cast<int>(report.updated.size())),
                              Tone::Ok);
            } else {
                auto* dialog = new FormDataReportDialog(path, report, view_.window());
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->open();
            }
            emit dataImported(path, report);
        });
}

// ---------------------------------------------------------------- отправка формы

void FormFillTool::submit(const forms::FieldAction& action) {
    const auto finish = [this](bool ok, const QString& message) {
        Toast::notify(view_.window(), message, ok ? Tone::Ok : Tone::Danger);
        emit submitted(ok, message);
    };
    if (view_.isProtectedView()) {
        finish(false, tr("В защищённом просмотре форма не отправляется. Включите все функции, если доверяете "
                         "файлу."));
        return;
    }
    forms::planSubmit(view_.session(), action).then(this, [this, finish](QFuture<forms::SubmitPlan> planned) {
        forms::SubmitPlan plan;
        try {
            plan = planned.result();
        } catch (const std::exception& e) {
            spdlog::warn("forms: submit plan failed: {}", e.what());
            finish(false, tr("Не удалось подготовить данные формы к отправке."));
            return;
        }
        if (plan.target == forms::SubmitTarget::Unsupported) {
            finish(false, tr("Адрес отправки не поддерживается: %1").arg(QString::fromStdString(plan.url)));
            return;
        }
        const bool yes = submitConfirm_ ? submitConfirm_(plan) : SubmitDialog::ask(view_.window(), plan);
        if (!yes) {
            return;  // без подтверждения ничего не уходит (эталон 7.4)
        }
        forms::submitBody(view_.session(), plan).then(this, [this, plan, finish](QFuture<QByteArray> done) {
            QByteArray body;
            try {
                body = done.result();
            } catch (const std::exception& e) {
                spdlog::warn("forms: submit body failed: {}", e.what());
                finish(false, tr("Не удалось подготовить документ к отправке."));
                return;
            }
            if (plan.target == forms::SubmitTarget::Mail) {
                const MailResult r = forms::sendMail(plan, body, mailer());
                switch (r.status) {
                    case MailResult::Status::Sent:
                        finish(true, tr("Письмо с данными формы создано."));
                        break;
                    case MailResult::Status::Cancelled:
                        finish(false, tr("Письмо не отправлено."));
                        break;
                    case MailResult::Status::NoClient:
                        finish(false, tr("Нет почтовой программы: сохраните данные формы («Экспорт данных…») "
                                         "и отправьте файл сами."));
                        break;
                    case MailResult::Status::Failed:
                        finish(false, tr("Не удалось создать письмо."));
                        break;
                }
                return;
            }
            QtConcurrent::run([plan, body] { return forms::sendHttp(plan, body, httpFetcher()); })
                .then(this, [this, finish](QFuture<net::Response> sent) {
                    const net::Response r = sent.result();
                    if (r.status == net::Status::Disabled) {
                        finish(false, tr("Сеть выключена (Установки → Защита): форма не отправлена."));
                        return;
                    }
                    if (!r.ok()) {
                        finish(false, r.httpCode > 0 ? tr("Сервер не принял форму (код %1).").arg(r.httpCode)
                                                     : tr("Форма не отправлена: сервер недоступен."));
                        return;
                    }
                    const std::string_view reply(reinterpret_cast<const char*>(r.body.data()), r.body.size());
                    if (!forms::isFdfResponse(r.contentType, reply)) {
                        finish(true, tr("Форма отправлена."));
                        return;
                    }
                    // Ответ сервера — данные формы (FDF): в форму.
                    forms::FormData data;
                    try {
                        data = forms::readData(reply, forms::DataFormat::Fdf);
                    } catch (const forms::DataError&) {
                        finish(true, tr("Форма отправлена."));
                        return;
                    }
                    forms::importData(view_.session(), std::move(data), QString())
                        .then(this, [finish](QFuture<forms::DataImportReport> imported) {
                            int n = 0;
                            try {
                                n = static_cast<int>(imported.result().updated.size());
                            } catch (const std::exception& e) {
                                spdlog::warn("forms: submit reply import failed: {}", e.what());
                            }
                            finish(true, tr("Форма отправлена. Сервер ответил данными формы: %1.").arg(n));
                        });
                });
        });
    });
}

}  // namespace ot::ui
