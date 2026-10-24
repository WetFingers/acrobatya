#include "ui/tools/certificate/certificate_tool.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <exception>

#include "core/sign/doc_timestamp.h"
#include "core/sign/ltv.h"
#include "core/sign/verifier.h"
#include "services/background_tasks.h"
#include "services/http_fetcher.h"
#include "services/settings.h"
#include "services/sign_ops.h"
#include "services/task_context.h"
#include "services/trust_service.h"
#include "services/tsa_settings.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shell/preferences/signatures_page.h"
#include "ui/shell/signature_status.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/certify_dialog.h"
#include "ui/tools/certificate/sign_dialog.h"
#include "ui/tools/certificate/sign_placement.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/tools/certificate/timestamp_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace certificate_tool {

MainWindow* windowOf(DocumentView* view) {
    return view != nullptr ? qobject_cast<MainWindow*>(view->window()) : nullptr;
}

struct ActionRow {
    CertificateTool::Action action;
    const char* icon;
    const char* name;
    const char* label;
};

constexpr ActionRow kActionRows[] = {
    {CertificateTool::Action::Sign, "signature", "certSign",
     QT_TRANSLATE_NOOP("ot::ui::CertificateTool", "Поставить цифровую подпись")},
    {CertificateTool::Action::CertifyVisible, "badge-check", "certCertifyVisible",
     QT_TRANSLATE_NOOP("ot::ui::CertificateTool", "Сертифицировать (видимая подпись)")},
    {CertificateTool::Action::CertifyInvisible, "award", "certCertifyInvisible",
     QT_TRANSLATE_NOOP("ot::ui::CertificateTool", "Сертифицировать (невидимая подпись)")},
    {CertificateTool::Action::Timestamp, "clock-check", "certTimestamp",
     QT_TRANSLATE_NOOP("ot::ui::CertificateTool", "Метка времени документа")},
};

}  // namespace certificate_tool

CertificateTool::CertificateTool(ToolRegistry& registry) : registry_(registry) {}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
CertificateTool::~CertificateTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString CertificateTool::title() const {
    return QCoreApplication::translate("ot::ui::ToolRegistry", "Использовать сертификат");
}

QString CertificateTool::icon() const { return QLatin1String(toolIconName("certificate")); }

SignPlacementTool& CertificateTool::placementOf(PageView& view) {
    ToolController& tools = view.tools();
    if (auto* tool = dynamic_cast<SignPlacementTool*>(tools.find(ViewToolId::Sign))) {
        return *tool;
    }
    return *static_cast<SignPlacementTool*>(tools.install(std::make_unique<SignPlacementTool>(view, tools)));
}

DocumentView* CertificateTool::view() const { return view_ != nullptr ? view_.data() : registry_.view(); }

void CertificateTool::activate(DocumentView& view) {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    view_ = &view;
    const auto refresh = [this] {
        refreshDocState();
        syncButtons();
    };
    connections_.push_back(connect(&view, &DocumentView::readOnlyChanged, this, refresh));
    connections_.push_back(connect(&view, &DocumentView::protectedViewChanged, this, refresh));
    connections_.push_back(
        connect(&view.session(), &DocumentSession::saved, this, refresh, Qt::QueuedConnection));
    // Состояние прежней вкладки не переносится: до ответа задачи — «не сертифицирован, без подписей» (ядро
    // всё равно откажет в сертификации подписанного — SignStatus::CannotCertify).
    mdp_ = sign::MdpPermission::None;
    hasSignatures_ = false;
    refreshDocState();
    syncButtons();
}

void CertificateTool::deactivate() {
    for (const QMetaObject::Connection& c : connections_) {
        QObject::disconnect(c);
    }
    connections_.clear();
    if (QCoreApplication::instance() == nullptr) {
        return;  // реестр — статический: после QApplication виджетов уже не трогать
    }
    endPlacement();
    view_ = nullptr;
    syncButtons();
}

// DocMDP и подписи вкладки — задачей; ответ меняет серые кнопки.
void CertificateTool::refreshDocState() {
    DocumentView* v = view();
    if (v == nullptr || v->isViewOnly()) {
        mdp_ = sign::MdpPermission::None;
        hasSignatures_ = false;
        return;
    }
    const quint64 request = ++stateRequest_;
    QPointer<DocumentView> target = v;
    signing::fields(v->session(), TaskPriority::Normal)
        .then(this, [this, request, target](QFuture<std::vector<sign::SignatureField>> done) {
            if (request != stateRequest_ || target == nullptr) {
                return;
            }
            bool any = false;
            try {
                for (const sign::SignatureField& f : done.result()) {
                    any = any || f.isSigned;
                }
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — подписей не видно
            }
            hasSignatures_ = any;
            signing::docMdp(target->session()).then(this, [this, request](QFuture<sign::DocMdp> mdp) {
                if (request != stateRequest_) {
                    return;
                }
                try {
                    mdp_ = mdp.result().permission;
                } catch (const std::exception&) {
                    mdp_ = sign::MdpPermission::None;
                }
                syncButtons();
                emit lockChanged();
            });
        });
}

QString CertificateTool::blockReason(Action action) const {
    const DocumentView* v = view();
    if (v == nullptr) {
        return tr("Откройте документ.");
    }
    if (busy_) {
        return tr("Подождите: идёт подписание.");
    }
    if (v->isViewOnly()) {
        return tr("Подписывать можно только PDF: сохраните файл как PDF.");
    }
    if (v->isProtectedView()) {
        return tr("Защищённый просмотр: подпись отключена. Нажмите «Включить все функции».");
    }
    if (v->isReadOnly()) {
        return tr("Документ открыт только для чтения: нажмите «Включить редактирование».");
    }
    if (!v->permissions().fillForms) {
        return tr("Документ защищён: заполнение форм и подпись запрещены.");
    }
    const bool certify = action == Action::CertifyVisible || action == Action::CertifyInvisible;
    if (action == Action::Sign && !sign::mdpAllows(mdp_, sign::MdpAction::Sign)) {
        return tr("Документ сертифицирован без права изменений: новые подписи запрещены.");
    }
    if (certify && mdp_ != sign::MdpPermission::None) {
        return tr("Документ уже сертифицирован.");
    }
    if (certify && hasSignatures_) {
        return tr(
            "Сертифицировать можно только документ без подписей: сертифицирующая подпись ставится "
            "первой.");
    }
    return {};
}

// ---------------------------------------------------------------- поток

bool CertificateTool::begin(Action action) {
    const QString reason = blockReason(action);
    if (!reason.isEmpty()) {
        if (DocumentView* v = view()) {
            Toast::notify(v->window(), reason, Tone::Warn);
        }
        return false;
    }
    endPlacement();
    return true;
}

bool CertificateTool::startSign() {
    if (!begin(Action::Sign)) {
        return false;
    }
    armPlacement(Action::Sign);
    return true;
}

bool CertificateTool::startCertify(bool visible) {
    const Action action = visible ? Action::CertifyVisible : Action::CertifyInvisible;
    if (!begin(action)) {
        return false;
    }
    if (visible) {
        armPlacement(action);
    } else {
        Pending p;
        p.action = action;
        p.page = view()->view().currentPage();
        openDialog(p);
    }
    return true;
}

void CertificateTool::armPlacement(Action action) {
    DocumentView& v = *view();
    SignPlacementTool& tool = placementOf(v.view());
    placing_ = Pending{action, 0, {}, {}};
    QPointer<SignPlacementTool> guard(&tool);
    signing::fields(v.session()).then(this, [guard](QFuture<std::vector<sign::SignatureField>> done) {
        if (guard == nullptr) {
            return;
        }
        try {
            guard->setFields(done.result());
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без полей — только рамка
        }
    });
    placementConnections_.push_back(connect(&tool, &SignPlacementTool::placed, this,
                                            [this](int page, ot::RectF rect, const QString& field) {
                                                if (!placing_) {
                                                    return;
                                                }
                                                Pending p = *placing_;
                                                p.page = page;
                                                p.rect = rect;
                                                p.field = field;
                                                endPlacement();
                                                openDialog(p);
                                            }));
    placementConnections_.push_back(connect(&tool, &SignPlacementTool::missed, this, [this] {
        if (DocumentView* d = view()) {
            Toast::notify(d->window(), tr("Протяните рамку подписи мышью или щёлкните пустое поле подписи."),
                          Tone::Info);
        }
    }));
    placementConnections_.push_back(
        connect(&tool, &SignPlacementTool::cancelled, this, [this] { endPlacement(); }));
    const bool certify = action == Action::CertifyVisible;
    placementBar_ = v.messageStack().add(
        Tone::Info, certify
                        ? tr("Сертификация: протяните рамку подписи на странице. Esc — отмена.")
                        : tr("Протяните рамку подписи на странице или щёлкните пустое поле подписи. Esc — "
                             "отмена."));
    placementBar_->setObjectName(QStringLiteral("signPlacementBar"));
    placementBar_->addAction(tr("Отмена"), QStringLiteral("cancel"));
    connect(placementBar_, &MessageBar::actionTriggered, this, [this](const QString&) {
        if (DocumentView* d = view()) {
            d->view().tools().setTool(ToolController::defaultTool());
        }
        endPlacement();
    });
    connect(placementBar_, &MessageBar::closed, this, [this] {
        placementBar_ = nullptr;
        if (DocumentView* d = view(); d != nullptr && d->view().tools().tool() == ViewToolId::Sign) {
            d->view().tools().setTool(ToolController::defaultTool());
        }
    });
    v.view().tools().setTool(ViewToolId::Sign);
    v.view().setFocus();
}

void CertificateTool::endPlacement() {
    placing_.reset();
    for (const QMetaObject::Connection& c : placementConnections_) {
        QObject::disconnect(c);
    }
    placementConnections_.clear();
    if (placementBar_ != nullptr) {
        MessageBar* bar = placementBar_;
        placementBar_ = nullptr;
        if (DocumentView* d = view()) {
            d->messageStack().removeBar(bar);
        }
    }
}

void CertificateTool::placeSignature(int page, const RectF& rect, const QString& field) {
    DocumentView* v = view();
    if (v == nullptr) {
        return;
    }
    if (!placing_ && !startSign()) {
        return;
    }
    placementOf(v->view()).place(page, rect, field);
}

void CertificateTool::openDialog(const Pending& pending) {
    DocumentView* v = view();
    if (v == nullptr) {
        return;
    }
    QWidget* parent = v->window();
    const QSizeF size(std::max(1.0f, pending.rect.width()), std::max(1.0f, pending.rect.height()));
    SignDialog* d = nullptr;
    if (pending.action == Action::CertifyVisible || pending.action == Action::CertifyInvisible) {
        d = new CertifyDialog(pending.action == Action::CertifyVisible, size, v->title(), parent);
    } else {
        SignDialog::Context c;
        c.mode = SignDialog::Mode::Sign;
        c.visible = !pending.rect.empty();
        c.rect = size;
        c.documentTitle = v->title();
        d = new SignDialog(c, parent);
    }
    d->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<DocumentView> target = v;
    d->setScanner([target]() -> QFuture<sign::WarningScan> {
        if (target == nullptr) {
            return QtFuture::makeReadyValueFuture(sign::WarningScan{});
        }
        return signing::warnings(target->session());
    });
    connect(d, &SignDialog::goToPage, this, [target](int page) {
        if (target != nullptr && page >= 0) {
            target->view().goToPage(page);
        }
    });
    connect(d, &QDialog::accepted, this, [this, d, pending] { signWith(*d, pending); });
    dialog_ = d;
    d->open();
    emit dialogOpened(d);
}

QString CertificateTool::chooseTarget(DocumentView& view) {
    const QString suggested = view.path();
    if (chooser_) {
        return chooser_(view.window(), suggested);
    }
    return QFileDialog::getSaveFileName(view.window(), tr("Сохранить подписанный документ"), suggested,
                                        tr("Документы PDF (*.pdf)"));
}

bool CertificateTool::askWithoutTimestamp(const QString& message) {
    if (timestampPrompt_) {
        return timestampPrompt_(message);
    }
    DocumentView* v = view();
    return QMessageBox::question(v != nullptr ? v->window() : nullptr, tr("Служба времени недоступна"),
                                 tr("Метка времени не получена: %1.\n\nПодписать без метки времени? Время "
                                    "подписи будет взято с часов этого компьютера (уровень B-B).")
                                     .arg(message),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void CertificateTool::signWith(SignDialog& dialog, const Pending& pending) {
    DocumentView* v = view();
    if (v == nullptr) {
        return;
    }
    const Settings settings;
    sign::SignRequest r;
    r.identity = dialog.identity();
    r.reason = dialog.reason().toStdString();
    r.location = dialog.location().toStdString();
    r.contact = dialog.contact().toStdString();
    r.subFilter = settings.get(settings::kSignCompatibility) ? sign::SubFilter::Pkcs7Detached
                                                             : sign::SubFilter::CadesDetached;
    r.placement.field = pending.field.toStdString();
    r.placement.page = pending.page;
    r.placement.rect = pending.action == Action::CertifyInvisible ? RectF{} : pending.rect;
    r.appearance = dialog.appearance();
    r.appearance.labels = SignTexts::appearanceLabels();
    r.lock = dialog.lockDocument();
    r.level = dialog.level();
    if (const std::optional<sign::TsaServer> server = dialog.tsaServer()) {
        r.timestamp = makeTsaClient(*server);
    }
    const sign::TimestampFallback fallback = timestampFallback(settings);
    QPointer<CertificateTool> self(this);
    r.withoutTimestamp = [self, fallback](const sign::TsaError& e) {
        if (fallback != sign::TimestampFallback::Ask) {
            return fallback == sign::TimestampFallback::WithoutTimestamp;
        }
        bool answer = false;
        const QString message = SignTexts::tsaStatus(e.status());
        if (self != nullptr) {
            // Вопрос — в окне: подпись идёт в потоке документа, он ждёт ответа.
            QMetaObject::invokeMethod(
                self.data(), [&answer, &self, &message] { answer = self->askWithoutTimestamp(message); },
                Qt::BlockingQueuedConnection);
        }
        return answer;
    };
    if (r.level == sign::SignLevel::LT || r.level == sign::SignLevel::LTA) {
        trust_ = TrustService::shared().snapshot();
        r.trust = trust_.get();
        r.revocationFetcher = &httpFetcher();
        r.vri = settings.get(settings::kLtvVri);
    }
    if (pending.action == Action::CertifyVisible || pending.action == Action::CertifyInvisible) {
        r.certify = dialog.permission();
    }
    const QString target = chooseTarget(*v);
    if (target.isEmpty()) {
        trust_.reset();
        emit finished(false, {});
        return;
    }
    busy_ = true;
    syncButtons();
    const quint64 task =
        BackgroundTasks::shared().start(tr("Подписание «%1»").arg(QFileInfo(target).fileName()));
    QPointer<DocumentView> signedView = v;
    v->session()
        .sign(std::move(r), target)
        .then(this, [this, target, task, signedView](QFuture<sign::SignResult> f) {
            BackgroundTasks::shared().finish(task);
            busy_ = false;
            trust_.reset();
            try {
                const sign::SignResult res = f.result();
                QString message = tr("Документ подписан и сохранён: %1.").arg(QFileInfo(target).fileName());
                if (!res.timestampError.empty()) {
                    message += QLatin1Char(' ') + tr("Метки времени нет — служба времени не ответила.");
                }
                if (!res.ltvError.empty()) {
                    message += QLatin1Char(' ') + tr("Сведения для проверки не добавлены.");
                }
                if (!res.archiveError.empty()) {
                    message += QLatin1Char(' ') + tr("Метка времени документа не добавлена.");
                }
                done(true, message);
                if (signedView != nullptr &&
                    (res.level == sign::SignLevel::B || res.level == sign::SignLevel::T)) {
                    offerLtv(*signedView);
                }
            } catch (const sign::SignError& e) {
                done(false, SignTexts::signError(e));
            } catch (const std::exception& e) {
                done(false, tr("Подписать не удалось: %1").arg(QString::fromUtf8(e.what())));
            }
        });
}

bool CertificateTool::startTimestamp() {
    if (!begin(Action::Timestamp)) {
        return false;
    }
    DocumentView* v = view();
    auto* d = new TimestampDialog(v->window());
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::accepted, this, [this, d] { timestampWith(*d); });
    connect(d, &TimestampDialog::serversRequested, this, [this] { showIdentities(); });
    dialog_ = d;
    d->open();
    emit dialogOpened(d);
    return true;
}

void CertificateTool::timestampWith(TimestampDialog& dialog) {
    DocumentView* v = view();
    const std::optional<sign::TsaServer> server = dialog.server();
    if (v == nullptr || !server) {
        return;
    }
    const QString target = chooseTarget(*v);
    if (target.isEmpty()) {
        emit finished(false, {});
        return;
    }
    busy_ = true;
    syncButtons();
    const quint64 task =
        BackgroundTasks::shared().start(tr("Метка времени «%1»").arg(QFileInfo(target).fileName()));
    v->session()
        .timestampDocument(makeTsaClient(*server), target)
        .then(this, [this, target, task](QFuture<sign::DocTimestampResult> f) {
            BackgroundTasks::shared().finish(task);
            busy_ = false;
            try {
                (void)f.result();
                done(true, tr("Метка времени документа добавлена: %1.").arg(QFileInfo(target).fileName()));
            } catch (const sign::SignError& e) {
                done(false, SignTexts::signError(e));
            } catch (const std::exception& e) {
                done(false, tr("Метку времени добавить не удалось: %1").arg(QString::fromUtf8(e.what())));
            }
        });
}

void CertificateTool::done(bool ok, const QString& message) {
    if (DocumentView* v = view(); v != nullptr && !message.isEmpty()) {
        Toast::notify(v->window(), message, ok ? Tone::Ok : Tone::Danger);
    }
    refreshDocState();
    syncButtons();
    emit finished(ok, message);
}

void CertificateTool::verifyAll() {
    DocumentView* v = view();
    if (v == nullptr || v->isViewOnly()) {
        return;
    }
    if (MainWindow* w = certificate_tool::windowOf(v)) {
        w->navPane().showPane(QStringLiteral("signatures"));
    }
    v->signatures().verify();  // итог — полоса и панель «Подписи» (ui/shell/signature_status.h)
}

void CertificateTool::showIdentities() {
    if (MainWindow* w = certificate_tool::windowOf(view())) {
        if (PreferencesDialog* d = w->showPreferences(QStringLiteral("signatures"))) {
            if (auto* page = qobject_cast<SignaturesPage*>(d->page(QStringLiteral("signatures")))) {
                page->showTab(QStringLiteral("identities"));
            }
        }
    }
}

// Сведения для долгосрочной проверки после подписи B-B/B-T (Установки → Подписи → Проверка,
// signatures/autoAddLtv): «всегда» — сразу; «спрашивать» — молча, если данных немного, иначе полосой с
// кнопкой (core decideAutoLtv); «не добавлять» — ничего.
void CertificateTool::offerLtv(DocumentView& view) {
    const QString mode = Settings().get(settings::kAutoAddLtv);
    if (mode == QLatin1String("never")) {
        return;
    }
    QPointer<DocumentView> target = &view;
    const auto add = [this, target] {
        if (target == nullptr) {
            return;
        }
        target->session()
            .addValidationInfo(TrustService::shared().snapshot(), Settings().get(settings::kLtvVri))
            .then(this, [this, target](QFuture<sign::LtvResult> f) {
                if (target == nullptr) {
                    return;
                }
                try {
                    const sign::LtvResult r = f.result();
                    if (r.dss.written && r.save.ok()) {
                        Toast::notify(target->window(), tr("Сведения для долгосрочной проверки добавлены."),
                                      Tone::Ok);
                    }
                    emit ltvFinished(r.dss.written && r.save.ok());
                } catch (const std::exception& e) {
                    Toast::notify(
                        target->window(),
                        tr("Сведения для проверки не добавлены: %1").arg(QString::fromUtf8(e.what())),
                        Tone::Danger);
                    emit ltvFinished(false);
                }
            });
    };
    signing::ltvDecision(view.session(), TrustService::shared().snapshot(),
                         sign::ltvAutoAddFromName(mode.toStdString()))
        .then(this, [this, target, add](QFuture<sign::LtvDecision> f) {
            sign::LtvDecision decision = sign::LtvDecision::Skip;
            try {
                decision = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): не собралось — не предлагать
            }
            if (target == nullptr) {
                return;  // вкладку закрыли — не о чем сообщать
            }
            if (decision == sign::LtvDecision::Skip) {
                emit ltvFinished(false);
            } else if (decision == sign::LtvDecision::Add) {
                add();
            } else {
                askLtv(*target, add);
            }
        });
}

void CertificateTool::askLtv(DocumentView& view, const std::function<void()>& add) {
    QPointer<DocumentView> target = &view;
    MessageBar* bar = view.messageStack().add(
        Tone::Info,
        tr("Добавить в документ сведения для долгосрочной проверки (LTV)? Подпись будет проверяться и "
           "после того, как сертификаты истекут."));
    bar->setObjectName(QStringLiteral("ltvOfferBar"));
    bar->addAction(tr("Добавить"), QStringLiteral("add"), true);
    connect(bar, &MessageBar::actionTriggered, this, [add, bar, target](const QString&) {
        if (target != nullptr) {
            target->messageStack().removeBar(bar);
        }
        add();
    });
}

// ---------------------------------------------------------------- панель

void CertificateTool::addCaption(QVBoxLayout* column, const QString& text) {
    auto* caption = new QLabel(text, panel_);
    caption->setObjectName(QStringLiteral("certSectionCaption"));
    column->addSpacing(Theme::sizes().grid * 2);
    column->addWidget(caption);
    captions_ << caption;
}

ToolButton* CertificateTool::addRow(QVBoxLayout* column, const QString& icon, Action action) {
    const certificate_tool::ActionRow* row = nullptr;
    for (const certificate_tool::ActionRow& r : certificate_tool::kActionRows) {
        if (r.action == action) {
            row = &r;
        }
    }
    auto* b = new ToolButton(icon, tr(row->label), panel_);
    b->setObjectName(QLatin1String(row->name));
    b->setLabelVisible(true);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setProperty("certAction", static_cast<int>(action));
    column->addWidget(b);
    buttons_ << b;
    return b;
}

QWidget* CertificateTool::panel() {
    if (panel_ == nullptr) {
        buildPanel();
        syncButtons();
    }
    return panel_;
}

void CertificateTool::buildPanel() {
    const Metrics& m = Theme::sizes();
    panel_ = new QWidget();
    panel_->setObjectName(QStringLiteral("certificateToolPanel"));
    auto* column = new QVBoxLayout(panel_);
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->setSpacing(m.grid);
    lockLabel_ = new QLabel(panel_);
    lockLabel_->setObjectName(QStringLiteral("certLockNote"));
    lockLabel_->setWordWrap(true);
    lockLabel_->setVisible(false);
    column->addWidget(lockLabel_);

    addCaption(column, tr("Подписать"));
    connect(addRow(column, QStringLiteral("signature"), Action::Sign), &ToolButton::clicked, this,
            [this] { startSign(); });
    addCaption(column, tr("Сертифицировать"));
    connect(addRow(column, QStringLiteral("badge-check"), Action::CertifyVisible), &ToolButton::clicked, this,
            [this] { startCertify(true); });
    connect(addRow(column, QStringLiteral("award"), Action::CertifyInvisible), &ToolButton::clicked, this,
            [this] { startCertify(false); });
    addCaption(column, tr("Время и проверка"));
    connect(addRow(column, QStringLiteral("clock-check"), Action::Timestamp), &ToolButton::clicked, this,
            [this] { startTimestamp(); });
    verifyButton_ = new ToolButton(QStringLiteral("shield-check"), tr("Проверить все подписи"), panel_);
    verifyButton_->setObjectName(QStringLiteral("certVerifyAll"));
    verifyButton_->setLabelVisible(true);
    verifyButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    verifyButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(verifyButton_, &ToolButton::clicked, this, [this] { verifyAll(); });
    column->addWidget(verifyButton_);
    addCaption(column, tr("Ещё"));
    identitiesButton_ = new ToolButton(QStringLiteral("id-card"), tr("Удостоверения и доверие…"), panel_);
    identitiesButton_->setObjectName(QStringLiteral("certIdentities"));
    identitiesButton_->setLabelVisible(true);
    identitiesButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    identitiesButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(identitiesButton_, &ToolButton::clicked, this, [this] { showIdentities(); });
    column->addWidget(identitiesButton_);
    column->addStretch(1);
}

ToolButton* CertificateTool::button(Action action) const {
    for (const QPointer<ToolButton>& b : buttons_) {
        if (b != nullptr && b->property("certAction").toInt() == static_cast<int>(action)) {
            return b;
        }
    }
    return nullptr;
}

void CertificateTool::syncButtons() {
    if (panel_ == nullptr) {
        return;
    }
    const DocumentView* v = view();
    for (const QPointer<ToolButton>& b : buttons_) {
        if (b == nullptr) {
            continue;
        }
        const auto action = static_cast<Action>(b->property("certAction").toInt());
        const QString reason = blockReason(action);
        b->setEnabled(reason.isEmpty());
        for (const certificate_tool::ActionRow& r : certificate_tool::kActionRows) {
            if (r.action == action) {
                b->setToolTip(reason.isEmpty() ? tr(r.label) : reason);
            }
        }
    }
    if (verifyButton_ != nullptr) {
        verifyButton_->setEnabled(v != nullptr && !v->isViewOnly());
    }
    if (lockLabel_ != nullptr) {
        // Строка панели — то, что запрещает подпись (иначе — что запрещает сертификацию сертифицированного).
        QString note = blockReason(Action::Sign);
        if (note.isEmpty() && mdp_ != sign::MdpPermission::None) {
            note = tr("Документ сертифицирован: %1.").arg(SignTexts::permission(mdp_).toLower());
        }
        if (v == nullptr || busy_) {
            note.clear();
        }
        lockLabel_->setText(note);
        lockLabel_->setVisible(!note.isEmpty());
    }
}

// ---------------------------------------------------------------- команды

std::vector<ot::Command> CertificateTool::commands() const {
    std::vector<ot::Command> out;
    ToolRegistry* registry = &registry_;
    auto* self = const_cast<CertificateTool*>(this);
    const auto run = [registry, self](void (*fn)(CertificateTool&)) {
        return [registry, self, fn] {
            if (DocumentView* v = registry->view()) {
                registry->activate(QStringLiteral("certificate"), v);
                fn(*self);
            }
        };
    };
    const auto hasDoc = [registry] { return registry->view() != nullptr && !registry->view()->isViewOnly(); };
    out.push_back({.id = QStringLiteral("sign.digital"),
                   .title = tr("Поставить &цифровую подпись…"),
                   .icon = QStringLiteral("signature"),
                   .run = run([](CertificateTool& t) { t.startSign(); }),
                   .enabled = hasDoc,
                   .menu = QStringLiteral("sign"),
                   .separatorBefore = true});
    out.push_back({.id = QStringLiteral("sign.certify"),
                   .title = tr("&Сертифицировать (видимая подпись)…"),
                   .icon = QStringLiteral("badge-check"),
                   .run = run([](CertificateTool& t) { t.startCertify(true); }),
                   .enabled = hasDoc,
                   .menu = QStringLiteral("sign")});
    out.push_back({.id = QStringLiteral("sign.certifyInvisible"),
                   .title = tr("Сертифицировать (&невидимая подпись)…"),
                   .icon = QStringLiteral("award"),
                   .run = run([](CertificateTool& t) { t.startCertify(false); }),
                   .enabled = hasDoc,
                   .menu = QStringLiteral("sign")});
    out.push_back({.id = QStringLiteral("sign.timestamp"),
                   .title = tr("&Метка времени документа…"),
                   .icon = QStringLiteral("clock-check"),
                   .run = run([](CertificateTool& t) { t.startTimestamp(); }),
                   .enabled = hasDoc,
                   .menu = QStringLiteral("sign")});
    out.push_back({.id = QStringLiteral("sign.validateAll"),
                   .title = tr("&Проверить все подписи"),
                   .icon = QStringLiteral("shield-check"),
                   .run = [self] { self->verifyAll(); },
                   .enabled = hasDoc,
                   .menu = QStringLiteral("sign"),
                   .separatorBefore = true});
    out.push_back({.id = QStringLiteral("sign.identities"),
                   .title = tr("&Удостоверения и доверие…"),
                   .icon = QStringLiteral("id-card"),
                   .run = [self] { self->showIdentities(); },
                   .enabled = [registry] { return registry->view() != nullptr; },
                   .menu = QStringLiteral("sign")});
    return out;
}

}  // namespace ot::ui
