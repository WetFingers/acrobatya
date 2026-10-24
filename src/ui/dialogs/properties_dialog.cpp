#include "ui/dialogs/properties_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

#include "services/document_session.h"
#include "ui/dialogs/xmp_editor.h"
#include "ui/shell/document_view.h"
#include "ui/theme/theme.h"
#include "ui/widgets/property_sheet.h"

#ifdef PropertySheet  // windows.h, включённый после property_sheet.h (unity)
#undef PropertySheet
#endif

namespace ot::ui {

namespace {

// «D:20260101120000+03'00'» → дата и время по-местному; не разобрать — как есть.
QString formatPdfDate(const std::string& raw) {
    QString s = QString::fromStdString(raw).trimmed();
    if (s.startsWith(QLatin1String("D:"))) {
        s = s.mid(2);
    }
    if (s.size() < 4) {
        return QString::fromStdString(raw);
    }
    auto num = [&](int pos, int len, int def) {
        bool ok = false;
        const int v = s.mid(pos, len).toInt(&ok);
        return ok && s.size() >= pos + len ? v : def;
    };
    const QDate date(num(0, 4, 0), num(4, 2, 1), num(6, 2, 1));
    const QTime time(num(8, 2, 0), num(10, 2, 0), num(12, 2, 0));
    if (!date.isValid() || !time.isValid()) {
        return QString::fromStdString(raw);
    }
    QTimeZone zone = QTimeZone::LocalTime;
    if (s.size() > 14) {
        const QChar sign = s.at(14);
        if (sign == QLatin1Char('Z')) {
            zone = QTimeZone::UTC;
        } else if (sign == QLatin1Char('+') || sign == QLatin1Char('-')) {
            const int seconds = (num(15, 2, 0) * 60 + num(18, 2, 0)) * 60;
            zone = QTimeZone::fromSecondsAheadOfUtc(sign == QLatin1Char('+') ? seconds : -seconds);
        }
    }
    const QDateTime dt(date, time, zone);
    return QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat);
}

QString yesNo(bool value) { return value ? PropertiesDialog::tr("Да") : PropertiesDialog::tr("Нет"); }

QString sizeText(std::uintmax_t bytes) {
    const QLocale locale;
    return PropertiesDialog::tr("%1 (%2 байт)")
        .arg(locale.formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat),
             locale.toString(static_cast<qulonglong>(bytes)));
}

// Пункты для вкладки «Начальный вид» (эталон 3.1; названия — как в русском Acrobat).
struct Choice {
    const char* pdf;
    const char* title;
};

const Choice kNavigation[] = {
    {"", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Только страница")},
    {"UseOutlines", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Панель закладок и страница")},
    {"UseThumbs", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Панель миниатюр и страница")},
    {"UseAttachments", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Панель вложений и страница")},
    {"UseOC", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Панель слоёв и страница")},
};

const Choice kLayouts[] = {
    {"", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "По умолчанию")},
    {"SinglePage", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Одна страница")},
    {"OneColumn", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Одна страница с прокруткой")},
    {"TwoPageLeft", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Разворот")},
    {"TwoColumnLeft", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Разворот с прокруткой")},
    {"TwoPageRight", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Разворот (титульная отдельно)")},
    {"TwoColumnRight",
     QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Разворот с прокруткой (титульная отдельно)")},
};

struct Magnification {
    links::DestFit fit;
    float zoom;
    const char* title;
};

const Magnification kMagnifications[] = {
    {links::DestFit::XYZ, 0, QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "По умолчанию")},
    {links::DestFit::Fit, 0, QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Уместить страницу")},
    {links::DestFit::FitH, 0, QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "По ширине")},
    {links::DestFit::FitV, 0, QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "По высоте")},
    {links::DestFit::FitBH, 0, QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Уместить видимое")},
    {links::DestFit::XYZ, 0.25f, "25 %"},
    {links::DestFit::XYZ, 0.5f, "50 %"},
    {links::DestFit::XYZ, 0.75f, "75 %"},
    {links::DestFit::XYZ, 1.0f, "100 %"},
    {links::DestFit::XYZ, 1.25f, "125 %"},
    {links::DestFit::XYZ, 1.5f, "150 %"},
    {links::DestFit::XYZ, 2.0f, "200 %"},
    {links::DestFit::XYZ, 4.0f, "400 %"},
};

const std::pair<const char*, const char*> kLanguages[] = {
    {"ru-RU", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Русский")},
    {"en-US", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Английский (США)")},
    {"en-GB", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Английский (Великобритания)")},
    {"uk-UA", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Украинский")},
    {"be-BY", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Белорусский")},
    {"de-DE", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Немецкий")},
    {"fr-FR", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Французский")},
    {"es-ES", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Испанский")},
    {"it-IT", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Итальянский")},
    {"zh-CN", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Китайский (упрощённый)")},
    {"ja-JP", QT_TRANSLATE_NOOP("ot::ui::PropertiesDialog", "Японский")},
};

QLabel* valueLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QWidget* scrolled(QWidget* content) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    return area;
}

bool sameInitial(const InitialView& a, const InitialView& b) {
    auto actionKey = [](const InitialView& v) {
        return v.openAction ? std::make_tuple(v.openAction->page, static_cast<int>(v.openAction->fit),
                                              std::lround(v.openAction->zoom * 1000))
                            : std::make_tuple(-1, 0, 0L);
    };
    return a.pageMode == b.pageMode && a.pageLayout == b.pageLayout && actionKey(a) == actionKey(b) &&
           a.hideToolbar == b.hideToolbar && a.hideMenubar == b.hideMenubar &&
           a.hideWindowUI == b.hideWindowUI && a.fitWindow == b.fitWindow &&
           a.centerWindow == b.centerWindow && a.displayDocTitle == b.displayDocTitle &&
           a.nonFullScreenPageMode == b.nonFullScreenPageMode && a.direction == b.direction;
}

bool sameInfo(const props::InfoFields& a, const props::InfoFields& b) {
    return a.title == b.title && a.author == b.author && a.subject == b.subject && a.keywords == b.keywords &&
           a.custom == b.custom;
}

}  // namespace

PropertiesDialog::PropertiesDialog(DocumentView& view, bool readOnly, QWidget* parent)
    : QDialog(parent),
      session_(view.sessionPtr()),
      pageCount_(view.session().pageCount()),
      readOnly_(readOnly) {
    setObjectName(QStringLiteral("propertiesDialog"));
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.dialogHeight);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("propertiesTabs"));
    tabs_->addTab(buildDescription(), QString());
    tabs_->addTab(buildSecurity(), QString());
    tabs_->addTab(buildFonts(), QString());
    tabs_->addTab(buildInitialView(), QString());
    tabs_->addTab(buildCustom(), QString());
    tabs_->addTab(buildAdvanced(), QString());
    column->addWidget(tabs_, 1);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == Fonts) {
            loadFonts();
        }
    });
    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Ok);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    ok_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &PropertiesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    retranslate();
    setEnabled(false);  // до загрузки

    QPointer<PropertiesDialog> self(this);
    props::readSheet(*session_).then(this, [self](const props::Sheet& sheet) {
        if (self != nullptr) {
            self->fill(sheet);
        }
    });
}

PropertiesDialog::~PropertiesDialog() = default;

void PropertiesDialog::showTab(Tab tab) { tabs_->setCurrentIndex(tab); }

QWidget* PropertiesDialog::buildDescription() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("propertiesDescription"));
    sheet->addSection(tr("Описание"));
    file_ = valueLabel(sheet);
    sheet->addRow(tr("Файл:"), file_);
    title_ = new QLineEdit(sheet);
    title_->setObjectName(QStringLiteral("propTitle"));
    sheet->addRow(tr("&Название:"), title_);
    author_ = new QLineEdit(sheet);
    author_->setObjectName(QStringLiteral("propAuthor"));
    sheet->addRow(tr("&Автор:"), author_);
    subject_ = new QLineEdit(sheet);
    subject_->setObjectName(QStringLiteral("propSubject"));
    sheet->addRow(tr("&Тема:"), subject_);
    keywords_ = new QLineEdit(sheet);
    keywords_->setObjectName(QStringLiteral("propKeywords"));
    sheet->addRow(tr("&Ключевые слова:"), keywords_);
    moreMetadata_ = new QPushButton(tr("&Дополнительные метаданные…"), sheet);
    moreMetadata_->setObjectName(QStringLiteral("propMoreMetadata"));
    connect(moreMetadata_, &QPushButton::clicked, this, [this] { showXmpEditor(); });
    sheet->addRow(QString(), moreMetadata_);
    sheet->addSection(tr("Сведения"));
    created_ = valueLabel(sheet);
    sheet->addRow(tr("Создан:"), created_);
    modified_ = valueLabel(sheet);
    sheet->addRow(tr("Изменён:"), modified_);
    creator_ = valueLabel(sheet);
    sheet->addRow(tr("Приложение:"), creator_);
    producer_ = valueLabel(sheet);
    sheet->addRow(tr("PDF-производитель:"), producer_);
    version_ = valueLabel(sheet);
    sheet->addRow(tr("Версия PDF:"), version_);
    location_ = valueLabel(sheet);
    sheet->addRow(tr("Расположение:"), location_);
    fileSize_ = valueLabel(sheet);
    sheet->addRow(tr("Размер файла:"), fileSize_);
    pageSize_ = valueLabel(sheet);
    sheet->addRow(tr("Размер страницы:"), pageSize_);
    pages_ = valueLabel(sheet);
    sheet->addRow(tr("Количество страниц:"), pages_);
    tagged_ = valueLabel(sheet);
    sheet->addRow(tr("Документ с тегами:"), tagged_);
    fastWeb_ = valueLabel(sheet);
    sheet->addRow(tr("Быстрый просмотр в Интернете:"), fastWeb_);
    return scrolled(sheet);
}

QWidget* PropertiesDialog::buildSecurity() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("propertiesSecurity"));
    sheet->addSection(tr("Защита документа"));
    method_ = valueLabel(sheet);
    method_->setObjectName(QStringLiteral("propSecurityMethod"));
    sheet->addRow(tr("Метод защиты:"), method_);
    encryption_ = valueLabel(sheet);
    sheet->addRow(tr("Шифрование:"), encryption_);
    changeSecurity_ = new QPushButton(tr("&Изменить…"), sheet);
    changeSecurity_->setEnabled(false);  // «Защитить» — сессия 7.6
    changeSecurity_->setToolTip(tr("«Изменить параметры защиты» появится в версии 0.7"));
    sheet->addRow(QString(), changeSecurity_);
    sheet->addSection(tr("Ограничения документа"));
    const char* const names[] = {QT_TR_NOOP("Печать:"),
                                 QT_TR_NOOP("Печать с высоким разрешением:"),
                                 QT_TR_NOOP("Изменение документа:"),
                                 QT_TR_NOOP("Сборка документа:"),
                                 QT_TR_NOOP("Копирование содержимого:"),
                                 QT_TR_NOOP("Копирование для специальных возможностей:"),
                                 QT_TR_NOOP("Комментирование:"),
                                 QT_TR_NOOP("Заполнение полей форм:"),
                                 QT_TR_NOOP("Подписание:")};
    for (int i = 0; i < 9; ++i) {
        rights_[i] = valueLabel(sheet);
        sheet->addRow(tr(names[i]), rights_[i]);
    }
    return scrolled(sheet);
}

QWidget* PropertiesDialog::buildFonts() {
    auto* page = new QWidget();
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(Theme::sizes().grid * 2, Theme::sizes().grid * 2, Theme::sizes().grid * 2,
                               Theme::sizes().grid * 2);
    auto* hint = new QLabel(tr("Шрифты, используемые в этом документе:"), page);
    column->addWidget(hint);
    fonts_ = new QTreeWidget(page);
    fonts_->setObjectName(QStringLiteral("propFonts"));
    fonts_->setHeaderHidden(true);
    column->addWidget(fonts_, 1);
    return page;
}

QWidget* PropertiesDialog::buildInitialView() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("propertiesInitialView"));
    sheet->addSection(tr("Макет и увеличение"));
    navigation_ = new QComboBox(sheet);
    navigation_->setObjectName(QStringLiteral("propNavigation"));
    for (const Choice& c : kNavigation) {
        navigation_->addItem(tr(c.title), QLatin1String(c.pdf));
    }
    sheet->addRow(tr("&Панель навигации:"), navigation_);
    layout_ = new QComboBox(sheet);
    layout_->setObjectName(QStringLiteral("propLayout"));
    for (const Choice& c : kLayouts) {
        layout_->addItem(tr(c.title), QLatin1String(c.pdf));
    }
    sheet->addRow(tr("&Макет страницы:"), layout_);
    magnification_ = new QComboBox(sheet);
    magnification_->setObjectName(QStringLiteral("propMagnification"));
    for (const Magnification& mg : kMagnifications) {
        magnification_->addItem(tr(mg.title));
    }
    sheet->addRow(tr("&Увеличение:"), magnification_);
    openPage_ = new QSpinBox(sheet);
    openPage_->setObjectName(QStringLiteral("propOpenPage"));
    openPage_->setRange(1, std::max(1, pageCount_));
    sheet->addRow(tr("Открыть на &странице:"), openPage_);
    sheet->addSection(tr("Параметры окна"));
    fitWindow_ = new QCheckBox(tr("Изменить размер окна по начальной странице"), sheet);
    sheet->addWidget(fitWindow_);
    centerWindow_ = new QCheckBox(tr("Центрировать окно на экране"), sheet);
    sheet->addWidget(centerWindow_);
    fullScreen_ = new QCheckBox(tr("Открыть в полноэкранном режиме"), sheet);
    fullScreen_->setObjectName(QStringLiteral("propFullScreen"));
    sheet->addWidget(fullScreen_);
    showTitle_ = new QComboBox(sheet);
    showTitle_->addItems({tr("Имя файла"), tr("Заголовок документа")});
    sheet->addRow(tr("Показ&ывать:"), showTitle_);
    sheet->addSection(tr("Параметры интерфейса"));
    hideMenubar_ = new QCheckBox(tr("Скрыть строку меню"), sheet);
    sheet->addWidget(hideMenubar_);
    hideToolbar_ = new QCheckBox(tr("Скрыть панели инструментов"), sheet);
    sheet->addWidget(hideToolbar_);
    hideWindowUi_ = new QCheckBox(tr("Скрыть элементы управления окна"), sheet);
    sheet->addWidget(hideWindowUi_);
    return scrolled(sheet);
}

QWidget* PropertiesDialog::buildCustom() {
    auto* page = new QWidget();
    const int g = Theme::sizes().grid;
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(g * 2, g * 2, g * 2, g * 2);
    column->addWidget(new QLabel(tr("Свои свойства документа (/Info и XMP):"), page));
    custom_ = new QTableWidget(0, 2, page);
    custom_->setObjectName(QStringLiteral("propCustom"));
    custom_->setHorizontalHeaderLabels({tr("Имя"), tr("Значение")});
    custom_->horizontalHeader()->setStretchLastSection(true);
    custom_->verticalHeader()->hide();
    custom_->setSelectionBehavior(QAbstractItemView::SelectRows);
    column->addWidget(custom_, 1);
    auto* row = new QHBoxLayout();
    addCustom_ = new QPushButton(tr("&Добавить"), page);
    connect(addCustom_, &QPushButton::clicked, this, [this] {
        addCustom(tr("Свойство"), QString());
        custom_->editItem(custom_->item(custom_->rowCount() - 1, 0));
    });
    removeCustom_ = new QPushButton(tr("&Удалить"), page);
    connect(removeCustom_, &QPushButton::clicked, this, [this] {
        const int r = custom_->currentRow();
        if (r >= 0) {
            custom_->removeRow(r);
        }
    });
    row->addWidget(addCustom_);
    row->addWidget(removeCustom_);
    row->addStretch(1);
    column->addLayout(row);
    return page;
}

QWidget* PropertiesDialog::buildAdvanced() {
    auto* sheet = new PropertySheet();
    sheet->setObjectName(QStringLiteral("propertiesAdvanced"));
    sheet->addSection(tr("Сведения о PDF"));
    advVersion_ = valueLabel(sheet);
    sheet->addRow(tr("Версия PDF:"), advVersion_);
    advLinearized_ = valueLabel(sheet);
    sheet->addRow(tr("Быстрый просмотр в Интернете:"), advLinearized_);
    advTagged_ = valueLabel(sheet);
    sheet->addRow(tr("Документ с тегами:"), advTagged_);
    advPdfa_ = valueLabel(sheet);
    sheet->addRow(tr("Соответствие PDF/A:"), advPdfa_);
    advSize_ = valueLabel(sheet);
    sheet->addRow(tr("Размер файла:"), advSize_);
    advObjects_ = valueLabel(sheet);
    sheet->addRow(tr("Объектов:"), advObjects_);
    advRepaired_ = valueLabel(sheet);
    sheet->addRow(tr("Файл восстанавливался:"), advRepaired_);
    sheet->addSection(tr("Параметры чтения"));
    baseUrl_ = new QLineEdit(sheet);
    baseUrl_->setObjectName(QStringLiteral("propBaseUrl"));
    baseUrl_->setPlaceholderText(QStringLiteral("https://"));
    sheet->addRow(tr("&Базовый URL:"), baseUrl_);
    language_ = new QComboBox(sheet);
    language_->setObjectName(QStringLiteral("propLanguage"));
    language_->setEditable(true);
    language_->addItem(QString(), QString());
    for (const auto& [code, name] : kLanguages) {
        language_->addItem(tr("%1 — %2").arg(tr(name), QLatin1String(code)), QLatin1String(code));
    }
    sheet->addRow(tr("&Язык:"), language_);
    binding_ = new QComboBox(sheet);
    binding_->setObjectName(QStringLiteral("propBinding"));
    binding_->addItem(tr("Левый край"), QStringLiteral("L2R"));
    binding_->addItem(tr("Правый край"), QStringLiteral("R2L"));
    sheet->addRow(tr("&Переплёт:"), binding_);
    sheet->addSection(tr("Проверка"));
    check_ = new QPushButton(tr("&Проверить документ"), sheet);
    check_->setObjectName(QStringLiteral("propCheck"));
    check_->setToolTip(tr("Читает все объекты файла и сообщает о повреждениях"));
    connect(check_, &QPushButton::clicked, this, &PropertiesDialog::runCheck);
    sheet->addRow(QString(), check_);
    warnings_ = new QPlainTextEdit(sheet);
    warnings_->setObjectName(QStringLiteral("propWarnings"));
    warnings_->setReadOnly(true);
    warnings_->setMaximumHeight(Theme::sizes().row * 4);
    warnings_->hide();
    sheet->addWidget(warnings_);
    return scrolled(sheet);
}

void PropertiesDialog::fill(const props::Sheet& s) {
    sheet_ = s;
    const QLocale locale;
    const QFileInfo info(s.path);
    file_->setText(QDir::toNativeSeparators(s.path));
    title_->setText(QString::fromStdString(s.info.title.empty() ? s.doc.title : s.info.title));
    author_->setText(QString::fromStdString(s.info.author.empty() ? s.doc.author : s.info.author));
    subject_->setText(QString::fromStdString(s.info.subject.empty() ? s.doc.subject : s.info.subject));
    keywords_->setText(QString::fromStdString(s.info.keywords.empty() ? s.doc.keywords : s.info.keywords));
    created_->setText(formatPdfDate(s.doc.creationDate));
    modified_->setText(formatPdfDate(s.doc.modDate));
    creator_->setText(QString::fromStdString(s.doc.creator));
    producer_->setText(QString::fromStdString(s.doc.producer));
    const QString version =
        s.doc.pdfVersion > 0 ? QStringLiteral("%1.%2").arg(s.doc.pdfVersion / 10).arg(s.doc.pdfVersion % 10)
                             : QString::fromStdString(s.doc.format);
    version_->setText(version);
    location_->setText(QDir::toNativeSeparators(info.absolutePath()));
    fileSize_->setText(sizeText(static_cast<std::uintmax_t>(std::max<qint64>(0, info.size()))));
    if (!s.firstPageSize.isEmpty()) {
        const double wmm = s.firstPageSize.width() * 25.4 / 72;
        const double hmm = s.firstPageSize.height() * 25.4 / 72;
        pageSize_->setText(tr("%1 × %2 мм (%3 × %4 дюйма)")
                               .arg(locale.toString(wmm, 'f', 0), locale.toString(hmm, 'f', 0),
                                    locale.toString(s.firstPageSize.width() / 72, 'f', 2),
                                    locale.toString(s.firstPageSize.height() / 72, 'f', 2)));
    }
    pages_->setText(locale.toString(s.doc.pageCount));
    tagged_->setText(yesNo(s.advanced.tagged));
    fastWeb_->setText(yesNo(s.doc.linearized));

    method_->setText(s.doc.encrypted ? tr("Защита паролем") : tr("Нет защиты"));
    encryption_->setText(s.doc.encrypted ? QString::fromStdString(s.doc.encryption) : tr("Нет"));
    const props::Rights& r = s.rights;
    const bool values[] = {r.print,         r.printHighRes, r.edit,      r.assemble, r.copy,
                           r.accessibility, r.annotate,     r.fillForms, r.fillForms};
    for (int i = 0; i < 9; ++i) {
        rights_[i]->setText(values[i] ? tr("Разрешено") : tr("Не разрешено"));
    }

    // Начальный вид.
    const InitialView& v = s.initial;
    const QString mode = QString::fromStdString(v.pageMode);
    const bool full = mode == QLatin1String("FullScreen");
    const QString panel = full ? QString::fromStdString(v.nonFullScreenPageMode) : mode;
    navigation_->setCurrentIndex(
        std::max(0, navigation_->findData(panel == QLatin1String("UseNone") ? QString() : panel)));
    layout_->setCurrentIndex(std::max(0, layout_->findData(QString::fromStdString(v.pageLayout))));
    int mag = 0;
    if (v.openAction) {
        openPage_->setValue(v.openAction->page + 1);
        for (int i = 0; i < static_cast<int>(std::size(kMagnifications)); ++i) {
            const Magnification& k = kMagnifications[i];
            const bool zoomMatch = std::abs(k.zoom - v.openAction->zoom) < 0.001f;
            if (k.fit == v.openAction->fit && (k.fit != links::DestFit::XYZ || zoomMatch)) {
                mag = i;
                break;
            }
        }
        if (v.openAction->fit == links::DestFit::FitB) {
            mag = 1;
        } else if (v.openAction->fit == links::DestFit::FitBV) {
            mag = 3;
        }
    } else {
        openPage_->setValue(1);
    }
    magnification_->setCurrentIndex(mag);
    fitWindow_->setChecked(v.fitWindow);
    centerWindow_->setChecked(v.centerWindow);
    fullScreen_->setChecked(full);
    showTitle_->setCurrentIndex(v.displayDocTitle ? 1 : 0);
    hideMenubar_->setChecked(v.hideMenubar);
    hideToolbar_->setChecked(v.hideToolbar);
    hideWindowUi_->setChecked(v.hideWindowUI);

    // Пользовательские.
    custom_->setRowCount(0);
    for (const auto& [key, value] : s.info.custom) {
        addCustom(QString::fromStdString(key), QString::fromStdString(value));
    }

    // Дополнительно.
    advVersion_->setText(version);
    advLinearized_->setText(yesNo(s.doc.linearized));
    advTagged_->setText(yesNo(s.advanced.tagged));
    advPdfa_->setText(s.doc.pdfa.empty() ? tr("Нет") : QString::fromStdString(s.doc.pdfa));
    advSize_->setText(sizeText(s.advanced.fileSize > 0
                                   ? s.advanced.fileSize
                                   : static_cast<std::uintmax_t>(std::max<qint64>(0, info.size()))));
    advObjects_->setText(locale.toString(s.advanced.objects));
    advRepaired_->setText(yesNo(s.advanced.repaired));
    baseUrl_->setText(QString::fromStdString(s.baseUrl));
    const QString lang = QString::fromStdString(s.lang);
    const int langIndex = language_->findData(lang);
    if (langIndex >= 0) {
        language_->setCurrentIndex(langIndex);
    } else {
        language_->setEditText(lang);
    }
    binding_->setCurrentIndex(v.direction == "R2L" ? 1 : 0);

    // Значения после загрузки — «ОК» сравнивает с ними.
    baseInfo_ = collectInfo();
    baseInitial_ = collectInitialView();
    baseLang_ = collectLanguage();
    baseUrlValue_ = baseUrl_->text().trimmed();
    baseDirection_ = collectDirection();
    loaded_ = true;
    setEnabled(true);
    setReadOnlyFields();
    if (tabs_->currentIndex() == Fonts) {
        loadFonts();
    }
    emit loaded();
}

void PropertiesDialog::setReadOnlyFields() {
    const bool ro = readOnly_ || !sheet_.isPdf;
    for (QLineEdit* e : {title_, author_, subject_, keywords_, baseUrl_}) {
        e->setReadOnly(ro);
    }
    for (QWidget* w :
         std::initializer_list<QWidget*>{navigation_, layout_, magnification_, openPage_, fitWindow_,
                                         centerWindow_, fullScreen_, showTitle_, hideMenubar_, hideToolbar_,
                                         hideWindowUi_, language_, binding_, addCustom_, removeCustom_}) {
        w->setEnabled(!ro);
    }
    custom_->setEditTriggers(ro ? QAbstractItemView::NoEditTriggers : QAbstractItemView::AllEditTriggers);
    moreMetadata_->setEnabled(sheet_.isPdf);
    check_->setEnabled(sheet_.isPdf);
}

void PropertiesDialog::loadFonts() {
    if (fontsRequested_ || !loaded_) {
        return;
    }
    fontsRequested_ = true;
    fonts_->clear();
    auto* waiting = new QTreeWidgetItem(fonts_);
    waiting->setText(0, tr("Поиск шрифтов…"));
    QPointer<PropertiesDialog> self(this);
    props::fonts(*session_).then(this, [self](const std::vector<props::FontInfo>& list) {
        if (self == nullptr) {
            return;
        }
        self->fonts_->clear();
        for (const props::FontInfo& f : list) {
            auto* item = new QTreeWidgetItem(self->fonts_);
            QString name = QString::fromStdString(f.name);
            QString state = f.subset     ? tr("встроенное подмножество")
                            : f.embedded ? tr("встроен")
                                         : tr("не встроен");
            item->setText(0, tr("%1 (%2)").arg(name, state));
            (new QTreeWidgetItem(item))->setText(0, tr("Тип: %1").arg(QString::fromStdString(f.type)));
            if (!f.encoding.empty()) {
                (new QTreeWidgetItem(item))
                    ->setText(0, tr("Кодировка: %1").arg(QString::fromStdString(f.encoding)));
            }
            (new QTreeWidgetItem(item))
                ->setText(0, tr("Впервые на странице %1, страниц: %2").arg(f.firstPage + 1).arg(f.pages));
        }
        if (list.empty()) {
            (new QTreeWidgetItem(self->fonts_))->setText(0, tr("Шрифтов нет"));
        }
        self->fontsLoaded_ = true;
        emit self->fontsReady();
    });
}

void PropertiesDialog::runCheck() {
    check_->setEnabled(false);
    warnings_->show();
    warnings_->setPlainText(tr("Проверка…"));
    QPointer<PropertiesDialog> self(this);
    props::check(*session_).then(this, [self](const props::Advanced& a) {
        if (self == nullptr) {
            return;
        }
        QStringList lines;
        for (const std::string& w : a.warnings) {
            lines << QString::fromStdString(w);
        }
        self->warnings_->setPlainText(lines.isEmpty() ? tr("Ошибок не найдено.")
                                                      : lines.join(QLatin1Char('\n')));
        self->advObjects_->setText(QLocale().toString(a.objects));
        self->advRepaired_->setText(yesNo(a.repaired));
        self->check_->setEnabled(true);
        emit self->checked();
    });
}

void PropertiesDialog::addCustom(const QString& name, const QString& value) {
    const int row = custom_->rowCount();
    custom_->insertRow(row);
    custom_->setItem(row, 0, new QTableWidgetItem(name));
    custom_->setItem(row, 1, new QTableWidgetItem(value));
}

props::InfoFields PropertiesDialog::collectInfo() const {
    props::InfoFields info = sheet_.info;
    info.title = title_->text().trimmed().toStdString();
    info.author = author_->text().trimmed().toStdString();
    info.subject = subject_->text().trimmed().toStdString();
    info.keywords = keywords_->text().trimmed().toStdString();
    info.custom.clear();
    for (int r = 0; r < custom_->rowCount(); ++r) {
        const QTableWidgetItem* k = custom_->item(r, 0);
        const QTableWidgetItem* v = custom_->item(r, 1);
        const QString key = k != nullptr ? k->text().trimmed() : QString();
        if (!key.isEmpty()) {
            info.custom[key.toStdString()] = v != nullptr ? v->text().toStdString() : std::string();
        }
    }
    return info;
}

InitialView PropertiesDialog::collectInitialView() const {
    InitialView v;
    const QString panel = navigation_->currentData().toString();
    if (fullScreen_->isChecked()) {
        v.pageMode = "FullScreen";
        v.nonFullScreenPageMode = panel.toStdString();
    } else {
        v.pageMode = panel.toStdString();
    }
    v.pageLayout = layout_->currentData().toString().toStdString();
    const int mag =
        std::clamp(magnification_->currentIndex(), 0, static_cast<int>(std::size(kMagnifications)) - 1);
    const int page = openPage_->value() - 1;
    if (page > 0 || mag > 0) {
        links::LinkAction a;
        a.kind = links::LinkKind::GoTo;
        a.page = page;
        a.fit = kMagnifications[mag].fit;
        a.zoom = kMagnifications[mag].zoom;
        v.openAction = a;
    }
    v.fitWindow = fitWindow_->isChecked();
    v.centerWindow = centerWindow_->isChecked();
    v.displayDocTitle = showTitle_->currentIndex() == 1;
    v.hideMenubar = hideMenubar_->isChecked();
    v.hideToolbar = hideToolbar_->isChecked();
    v.hideWindowUI = hideWindowUi_->isChecked();
    v.direction = collectDirection().toStdString();
    return v;
}

QString PropertiesDialog::collectLanguage() const {
    const QString text = language_->currentText().trimmed();
    const int index = language_->findText(text);
    return index >= 0 ? language_->itemData(index).toString() : text;
}

QString PropertiesDialog::collectDirection() const {
    // L2R — по умолчанию: ключ не пишется, если его не было.
    const QString d = binding_->currentData().toString();
    if (d == QLatin1String("L2R") && sheet_.initial.direction != "L2R") {
        return {};
    }
    return d;
}

props::Changes PropertiesDialog::changes() const {
    props::Changes c;
    if (!loaded_ || readOnly_ || !sheet_.isPdf) {
        return c;
    }
    const props::InfoFields info = collectInfo();
    if (!sameInfo(info, baseInfo_)) {
        c.info = info;
    }
    const InitialView initial = collectInitialView();
    if (!sameInitial(initial, baseInitial_)) {
        c.initial = initial;
    }
    const QString lang = collectLanguage();
    if (lang != baseLang_) {
        c.lang = lang.toStdString();
    }
    const QString url = baseUrl_->text().trimmed();
    if (url != baseUrlValue_) {
        c.baseUrl = url.toStdString();
    }
    return c;
}

QFuture<void> PropertiesDialog::applyChanges() {
    const props::Changes c = changes();
    if (c.empty()) {
        return QtFuture::makeReadyVoidFuture();
    }
    QPointer<PropertiesDialog> self(this);
    QFuture<void> f = props::apply(*session_, c);
    f.then(this, [self] {
        if (self != nullptr) {
            emit self->applied();
        }
    });
    return f;
}

void PropertiesDialog::accept() {
    applyChanges();
    QDialog::accept();
}

XmpEditor* PropertiesDialog::showXmpEditor() {
    if (xmp_ == nullptr) {
        xmp_ = new XmpEditor(this);
        connect(xmp_, &QDialog::accepted, this, [this] {
            // Описание и свои свойства — в поля диалога: «ОК» «Свойств» запишет их вместе.
            title_->setText(xmp_->title());
            author_->setText(xmp_->author());
            subject_->setText(xmp_->subject());
            keywords_->setText(xmp_->keywords());
            custom_->setRowCount(0);
            for (const auto& [name, value] : xmp_->customProperties()) {
                addCustom(name, value);
            }
        });
    }
    QList<std::pair<QString, QString>> custom;
    for (int r = 0; r < custom_->rowCount(); ++r) {
        const QTableWidgetItem* k = custom_->item(r, 0);
        const QTableWidgetItem* v = custom_->item(r, 1);
        if (k != nullptr && !k->text().trimmed().isEmpty()) {
            custom << std::make_pair(k->text().trimmed(), v != nullptr ? v->text() : QString());
        }
    }
    xmp_->load(sheet_.xmp, title_->text(), author_->text(), subject_->text(), keywords_->text(), custom,
               readOnly_ || !sheet_.isPdf);
    xmp_->open();
    return xmp_;
}

XmpEditor* PropertiesDialog::xmpEditor() const { return xmp_; }

void PropertiesDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void PropertiesDialog::retranslate() {
    setWindowTitle(tr("Свойства документа"));
    tabs_->setTabText(Description, tr("Описание"));
    tabs_->setTabText(Security, tr("Защита"));
    tabs_->setTabText(Fonts, tr("Шрифты"));
    tabs_->setTabText(InitialViewTab, tr("Начальный вид"));
    tabs_->setTabText(Custom, tr("Пользовательские"));
    tabs_->setTabText(Advanced, tr("Дополнительно"));
    ok_->setText(tr("ОК"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
