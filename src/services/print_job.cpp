#include "services/print_job.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QPrinter>
#include <QPrinterInfo>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <string>

#include "core/doc/permissions.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/print/band_renderer.h"
#include "core/render/display_cache.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/task_context.h"

namespace ot {

namespace {

constexpr qreal kFramePt = 0.5;  // рамки n-up и метки реза
constexpr qreal kLabelPt = 7.0;  // подпись плитки постера
constexpr int kListCache = 8;    // display lists задания (n-up и копии рисуют страницу снова)
const QString kMsPrintToPdf = QStringLiteral("Microsoft Print to PDF");

// ---- Подмена принтеров
// ----------------------------------------------------------------------------------------

struct Override {
    QStringList printers;
    PrintDevices::Factory factory;
    Override* previous = nullptr;
};
Override* g_override = nullptr;  // только GUI-поток (тесты, окно)

QString fakeDir() { return qEnvironmentVariable("OTTISK_FAKE_PRINTER"); }

QString safeName(QString name) {
    for (QChar& c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) {
            c = QLatin1Char('_');
        }
    }
    return name.isEmpty() ? QStringLiteral("printer") : name;
}

// ---- Устройства
// ---------------------------------------------------------------------------------------------------

QPageLayout::Orientation orientationOf(bool landscape) {
    return landscape ? QPageLayout::Landscape : QPageLayout::Portrait;
}

class PrinterDevice final : public PrintDevice {
public:
    explicit PrinterDevice(QPrinter& printer) : printer_(printer) {}
    ~PrinterDevice() override {
        if (painter_.isActive()) {
            painter_.end();
        }
    }

    QString name() const override { return printer_.printerName(); }

    print::Paper paper() const override {
        const QPageLayout::Orientation original = printer_.pageLayout().orientation();
        const auto measure = [this](bool landscape, print::SizeF* size) {
            printer_.setPageOrientation(orientationOf(landscape));
            QPageLayout layout = printer_.pageLayout();
            layout.setUnits(QPageLayout::Point);
            const QRectF full = layout.fullRect();
            const QMarginsF m = layout.minimumMargins();
            if (size != nullptr) {
                *size = {static_cast<float>(full.width()), static_cast<float>(full.height())};
            }
            return RectF{static_cast<float>(m.left()), static_cast<float>(m.top()),
                         static_cast<float>(full.width() - m.right()),
                         static_cast<float>(full.height() - m.bottom())};
        };
        print::Paper paper;
        paper.printable = measure(false, &paper.size);
        paper.printableLandscape = measure(true, nullptr);
        printer_.setPageOrientation(original);
        return paper;
    }

    int resolution() const override { return printer_.resolution(); }
    bool supportsCopies() const override { return printer_.supportsMultipleCopies(); }
    bool supportsDuplex() const override {
        return QPrinterInfo(printer_).supportedDuplexModes().contains(QPrinter::DuplexLongSide);
    }

    bool begin(const PrintDeviceOptions& options, bool landscape) override {
        printer_.setDocName(options.title);
        printer_.setFullPage(true);
        printer_.setPageOrientation(orientationOf(landscape));
        landscape_ = landscape;
        if (printer_.supportsMultipleCopies()) {
            printer_.setCopyCount(std::max(1, options.copies));
            printer_.setCollateCopies(options.collate);
        }
        switch (options.duplex) {
            case DuplexMode::None:
                printer_.setDuplex(QPrinter::DuplexNone);
                break;
            case DuplexMode::LongSide:
                printer_.setDuplex(QPrinter::DuplexLongSide);
                break;
            case DuplexMode::ShortSide:
                printer_.setDuplex(QPrinter::DuplexShortSide);
                break;
        }
        if (options.paperSource >= 0) {
            printer_.setPaperSource(static_cast<QPrinter::PaperSource>(options.paperSource));
        }
        if (!painter_.begin(&printer_)) {
            error_ = QStringLiteral("cannot start print job on \"%1\"").arg(printer_.printerName());
            return false;
        }
        return true;
    }

    bool newSheet(bool landscape) override {
        if (landscape != landscape_) {
            printer_.setPageOrientation(orientationOf(landscape));  // перед newPage — для нового листа
            landscape_ = landscape;
        }
        if (!printer_.newPage()) {
            error_ = QStringLiteral("printer rejected a new page");
            return false;
        }
        return true;
    }

    void drawImage(const QRectF& target, const QImage& image) override { painter_.drawImage(target, image); }
    void drawLine(const QPointF& a, const QPointF& b, qreal width) override {
        painter_.setPen(QPen(Qt::black, width));
        painter_.drawLine(a, b);
    }
    void drawFrame(const QRectF& rect, qreal width) override {
        painter_.setPen(QPen(Qt::black, width));
        painter_.setBrush(Qt::NoBrush);
        painter_.drawRect(rect);
    }
    void drawLabel(const QRectF& rect, const QString& text, qreal pixelSize) override {
        QFont font = painter_.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixelSize))));
        painter_.setFont(font);
        painter_.setPen(Qt::black);
        painter_.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    bool end() override {
        const bool ok = painter_.end();
        if (!ok || printer_.printerState() == QPrinter::Error) {
            error_ = QStringLiteral("printer reported an error");
            return false;
        }
        return true;
    }
    void abort() override {
        printer_.abort();
        if (painter_.isActive()) {
            painter_.end();
        }
    }
    QString errorString() const override { return error_; }

private:
    QPrinter& printer_;
    QPainter painter_;
    bool landscape_ = false;
    QString error_;
};

class PdfDevice final : public PrintDevice {
public:
    PdfDevice(QString path, print::SizeF size, int resolution)
        : path_(std::move(path)), size_(size), resolution_(resolution) {}
    ~PdfDevice() override {
        if (painter_.isActive()) {
            painter_.end();
        }
    }

    QString name() const override { return QStringLiteral("PDF"); }
    print::Paper paper() const override { return print::Paper::withMargins(size_, 0); }
    int resolution() const override { return resolution_; }

    bool begin(const PrintDeviceOptions& options, bool landscape) override {
        QDir().mkpath(QFileInfo(path_).absolutePath());
        writer_ = std::make_unique<QPdfWriter>(path_);
        writer_->setResolution(resolution_);
        writer_->setTitle(options.title);
        writer_->setCreator(QStringLiteral("AcroBatya"));
        setLayout(landscape);
        if (!painter_.begin(writer_.get())) {
            error_ = QStringLiteral("cannot write \"%1\"").arg(path_);
            return false;
        }
        return true;
    }
    bool newSheet(bool landscape) override {
        setLayout(landscape);
        return writer_->newPage();
    }
    void drawImage(const QRectF& target, const QImage& image) override { painter_.drawImage(target, image); }
    void drawLine(const QPointF& a, const QPointF& b, qreal width) override {
        painter_.setPen(QPen(Qt::black, width));
        painter_.drawLine(a, b);
    }
    void drawFrame(const QRectF& rect, qreal width) override {
        painter_.setPen(QPen(Qt::black, width));
        painter_.setBrush(Qt::NoBrush);
        painter_.drawRect(rect);
    }
    void drawLabel(const QRectF& rect, const QString& text, qreal pixelSize) override {
        QFont font = painter_.font();
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixelSize))));
        painter_.setFont(font);
        painter_.setPen(Qt::black);
        painter_.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    bool end() override {
        if (!painter_.end()) {
            error_ = QStringLiteral("cannot finish \"%1\"").arg(path_);
            return false;
        }
        writer_.reset();
        return true;
    }
    void abort() override {
        if (painter_.isActive()) {
            painter_.end();
        }
        writer_.reset();
        QFile::remove(path_);
    }
    QString errorString() const override { return error_; }

private:
    void setLayout(bool landscape) {
        const QPageSize size(QSizeF(size_.width, size_.height), QPageSize::Point, QString(),
                             QPageSize::ExactMatch);
        writer_->setPageLayout(QPageLayout(size, orientationOf(landscape), QMarginsF(0, 0, 0, 0)));
    }

    QString path_;
    print::SizeF size_;
    int resolution_;
    std::unique_ptr<QPdfWriter> writer_;
    QPainter painter_;
    QString error_;
};

// Порядок листов с копиями, которые печатает программа: подбор — задание целиком N раз (с дуплексом и
// нечётным числом листов — пустой оборот между копиями); без подбора — каждый лист (с дуплексом — пара
// лицо + оборот) N раз. -1 — пустой лист.
std::vector<int> sheetOrder(int sheets, int copies, bool collate, bool duplex) {
    std::vector<int> order;
    copies = std::max(1, copies);
    if (collate || copies == 1) {
        for (int c = 0; c < copies; ++c) {
            for (int i = 0; i < sheets; ++i) {
                order.push_back(i);
            }
            if (duplex && sheets % 2 == 1 && c + 1 < copies) {
                order.push_back(-1);
            }
        }
        return order;
    }
    const int unit = duplex ? 2 : 1;
    for (int i = 0; i < sheets; i += unit) {
        for (int c = 0; c < copies; ++c) {
            for (int j = i; j < i + unit; ++j) {
                order.push_back(j < sheets ? j : -1);
            }
        }
    }
    return order;
}

}  // namespace

// ---- FakePrintDevice
// ----------------------------------------------------------------------------------------------

FakePrintDevice::FakePrintDevice(QString name, int resolution, print::Paper paper)
    : name_(std::move(name)), resolution_(resolution), paper_(paper) {}

void FakePrintDevice::addSheet(bool landscape) {
    const print::SizeF s = paper_.oriented(landscape);
    const qreal k = resolution_ / 72.0;
    Sheet sheet;
    sheet.landscape = landscape;
    sheet.image = QImage(static_cast<int>(std::ceil(s.width * k)), static_cast<int>(std::ceil(s.height * k)),
                         QImage::Format_RGB32);
    sheet.image.fill(Qt::white);
    sheets.push_back(sheet);
}

bool FakePrintDevice::begin(const PrintDeviceOptions& opts, bool landscape) {
    options = opts;
    begun = true;
    addSheet(landscape);
    return true;
}

bool FakePrintDevice::newSheet(bool landscape) {
    addSheet(landscape);
    return true;
}

void FakePrintDevice::drawImage(const QRectF& target, const QImage& image) {
    QPainter p(&sheets.back().image);
    p.drawImage(target, image);
    ++sheets.back().images;
    sheets.back().imageSizes << image.size();
}

void FakePrintDevice::drawLine(const QPointF& a, const QPointF& b, qreal width) {
    QPainter p(&sheets.back().image);
    p.setPen(QPen(Qt::black, width));
    p.drawLine(a, b);
    ++sheets.back().lines;
}

void FakePrintDevice::drawFrame(const QRectF& rect, qreal width) {
    QPainter p(&sheets.back().image);
    p.setPen(QPen(Qt::black, width));
    p.drawRect(rect);
    ++sheets.back().frames;
}

void FakePrintDevice::drawLabel(const QRectF& /*rect*/, const QString& text, qreal /*pixelSize*/) {
    sheets.back().labels << text;  // без шрифта: тесты служб идут на QCoreApplication
}

bool FakePrintDevice::end() {
    ended = true;
    return true;
}

// ---- Фабрики
// ------------------------------------------------------------------------------------------------------

std::unique_ptr<PrintDevice> makePrinterDevice(QPrinter& printer) {
    return std::make_unique<PrinterDevice>(printer);
}

std::unique_ptr<PrintDevice> makePdfDevice(const QString& path, print::SizeF pageSize, int resolution) {
    return std::make_unique<PdfDevice>(path, pageSize, resolution);
}

QStringList PrintDevices::printers() {
    if (g_override != nullptr) {
        return g_override->printers;
    }
    if (!fakeDir().isEmpty()) {
        return {QStringLiteral("Fake Printer"), kMsPrintToPdf};
    }
    return QPrinterInfo::availablePrinterNames();
}

QString PrintDevices::defaultPrinter() {
    if (g_override != nullptr) {
        return g_override->printers.value(0);
    }
    if (!fakeDir().isEmpty()) {
        return QStringLiteral("Fake Printer");
    }
    return QPrinterInfo::defaultPrinterName();
}

bool PrintDevices::supportsDuplex(const QString& printer) {
    if (overridden()) {
        return false;
    }
    return QPrinterInfo::printerInfo(printer).supportedDuplexModes().contains(QPrinter::DuplexLongSide);
}

bool PrintDevices::overridden() { return g_override != nullptr || !fakeDir().isEmpty(); }

std::unique_ptr<PrintDevice> PrintDevices::open(const QString& printer, const QString& output) {
    const QString name = printer.isEmpty() ? defaultPrinter() : printer;
    if (g_override != nullptr) {
        return g_override->factory ? g_override->factory(name, output) : nullptr;
    }
    if (const QString dir = fakeDir(); !dir.isEmpty()) {
        QDir().mkpath(dir);
        const QString base = safeName(name);
        int n = 1;
        QString file;
        do {
            file = QDir(dir).filePath(QStringLiteral("%1-%2.pdf").arg(base).arg(n++));
        } while (QFileInfo::exists(file));
        QFile log(QDir(dir).filePath(QStringLiteral("jobs.jsonl")));
        if (log.open(QIODevice::Append)) {
            const QJsonObject line{{QStringLiteral("printer"), name},
                                   {QStringLiteral("output"), output},
                                   {QStringLiteral("file"), QFileInfo(file).fileName()}};
            log.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
        }
        return makePdfDevice(file);
    }
    if (!output.isEmpty() && name.compare(kMsPrintToPdf, Qt::CaseInsensitive) == 0) {
        return makeGdiDevice(name, output);
    }
    return nullptr;
}

ScopedPrintDevices::ScopedPrintDevices(QStringList printers, PrintDevices::Factory factory) {
    auto* o = new Override{std::move(printers), std::move(factory), g_override};
    g_override = o;
}

ScopedPrintDevices::~ScopedPrintDevices() {
    Override* o = g_override;
    g_override = o->previous;
    delete o;
}

// ---- План
// ---------------------------------------------------------------------------------------------------------

print::SizeMode printScaleFromName(const QString& name) {
    if (name == QLatin1String("actual")) {
        return print::SizeMode::Actual;
    }
    if (name == QLatin1String("shrink")) {
        return print::SizeMode::ShrinkOversized;
    }
    return print::SizeMode::Fit;
}

PrintPlan makePrintPlan(const OpenSummary& doc, const PrintSettings& settings, const print::Paper& paper) {
    PrintPlan plan;
    if (!doc.permissions.print) {
        plan.status = PrintStatus::Denied;
        return plan;
    }
    plan.sizes.reserve(static_cast<std::size_t>(doc.pageSizes.size()));
    for (const QSizeF& s : doc.pageSizes) {
        plan.sizes.push_back({static_cast<float>(s.width()), static_cast<float>(s.height())});
    }
    std::vector<std::string> labels;
    labels.reserve(static_cast<std::size_t>(doc.pageLabels.size()));
    for (const QString& l : doc.pageLabels) {
        labels.push_back(l.toStdString());
    }
    const auto pages = print::resolvePages(settings.pages, doc.pageCount, labels);
    if (!pages) {
        plan.status = PrintStatus::RangeError;
        return plan;
    }
    if (pages->empty()) {
        plan.status = PrintStatus::NothingToPrint;
        return plan;
    }
    plan.pages = *pages;
    print::ImposeOptions layout = settings.layout;
    if (layout.title.empty()) {
        layout.title = settings.title.toStdString();
    }
    plan.sheets = print::impose(plan.sizes, plan.pages, paper, layout);
    plan.duplex =
        layout.layout == print::Layout::Booklet && layout.bookletSubset == print::BookletSubset::BothSides
            ? DuplexMode::ShortSide
            : settings.duplex;
    return plan;
}

int printDpi(const PrintSettings& settings, int resolution, bool printHighRes) {
    int dpi = settings.asImage ? settings.imageDpi : std::min(resolution, settings.maxDpi);
    dpi = std::min(dpi, resolution);
    if (!printHighRes) {
        dpi = std::min(dpi, 150);
    }
    return std::max(dpi, 1);
}

// ---- Задание
// ------------------------------------------------------------------------------------------------------

struct PrintJob::Cancel {
    std::atomic<bool> flag{false};
    fz_cookie cookie{};
};

PrintJob::PrintJob(DocumentSession& session, PrintSettings settings)
    : session_(session), settings_(std::move(settings)), cancel_(std::make_unique<Cancel>()) {}

PrintJob::~PrintJob() = default;

bool PrintJob::cancelled() const noexcept { return cancel_->flag.load(); }

void PrintJob::cancel() {
    cancel_->flag.store(true);
    cancel_->cookie.abort = 1;
}

PrintStatus PrintJob::run(PrintDevice& device, const std::function<void(int, int)>& progress) {
    error_.clear();
    printed_ = 0;
    const OpenSummary summary = session_.summary();
    if (!summary.ok) {
        error_ = QStringLiteral("no document");
        return PrintStatus::Failed;
    }
    const print::Paper paper = device.paper();
    const PrintPlan plan = makePrintPlan(summary, settings_, paper);
    if (plan.status != PrintStatus::Done) {
        return plan.status;
    }
    const int deviceDpi = std::max(1, device.resolution());
    const int dpi = printDpi(settings_, deviceDpi, summary.permissions.printHighRes);
    const qreal k = static_cast<qreal>(deviceDpi) / dpi;  // пиксель растра → пиксель устройства
    const qreal pt = deviceDpi / 72.0;                    // пункт листа → пиксель устройства

    PrintDeviceOptions options;
    options.title = settings_.title;
    options.duplex = plan.duplex;
    options.paperSource = settings_.paperSource;
    const bool driverCopies = device.supportsCopies();
    options.copies = driverCopies ? std::max(1, settings_.copies) : 1;
    options.collate = settings_.collate;
    const std::vector<int> order =
        sheetOrder(static_cast<int>(plan.sheets.size()), driverCopies ? 1 : settings_.copies,
                   settings_.collate, plan.duplex != DuplexMode::None);
    if (order.empty()) {
        return PrintStatus::NothingToPrint;
    }
    spdlog::info("print: \"{}\" on \"{}\": {} pages, {} sheets, {} dpi (device {})",
                 settings_.title.toStdString(), device.name().toStdString(), plan.pages.size(), order.size(),
                 dpi, deviceDpi);
    if (!device.begin(options, plan.sheets[static_cast<std::size_t>(order.front())].landscape)) {
        error_ = device.errorString();
        spdlog::warn("print: {}", error_.toStdString());
        return PrintStatus::Failed;
    }

    std::map<int, std::shared_ptr<const DisplayList>> lists;
    std::vector<int> listOrder;
    const auto listFor = [&](int page) {
        if (const auto it = lists.find(page); it != lists.end()) {
            return it->second;
        }
        const print::PrintContent content = settings_.content;
        auto future = session_.post(
            [page, content](TaskContext& tc) {
                return print::buildPrintList(tc.document(), page, content, tc.cookie());
            },
            TaskPriority::Normal, QStringLiteral("print"));
        std::shared_ptr<const DisplayList> list = future.result();  // исключение задачи — здесь
        lists[page] = list;
        listOrder.push_back(page);
        if (static_cast<int>(listOrder.size()) > kListCache) {
            lists.erase(listOrder.front());
            listOrder.erase(listOrder.begin());
        }
        return list;
    };
    fz_context* ctx = MuContext::forThisThread().get();
    print::BandOptions bands;
    bands.dpi = static_cast<float>(dpi);
    bands.grayscale = settings_.grayscale;

    const auto drawSheet = [&](const print::Sheet& sheet) {
        for (const print::Placement& planned : sheet.placements) {
            if (planned.page < 0 || planned.clip.empty()) {
                continue;
            }
            const auto list = listFor(planned.page);
            print::Placement p = planned;
            // План считал по размерам раскладки (OpenSummary: > 14 400 pt уменьшены) — масштаб к рамке
            // страницы.
            const float plannedWidth = plan.sizes[static_cast<std::size_t>(p.page)].width;
            if (list->bounds().width() > 0 && plannedWidth > 0) {
                p.scale *= plannedWidth / list->bounds().width();
            }
            print::renderPlacement(ctx, *list, p, bands, &cancel_->cookie, [&](const print::Band& b) {
                const QImage image(
                    b.pixels, b.area.width(), b.area.height(), b.stride,
                    b.format == PixelFormat::Gray8 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
                device.drawImage(
                    QRectF(b.area.x0 * k, b.area.y0 * k, b.area.width() * k, b.area.height() * k), image);
            });
        }
        for (const RectF& r : sheet.frames) {
            device.drawFrame(QRectF(r.x0 * pt, r.y0 * pt, r.width() * pt, r.height() * pt), kFramePt * pt);
        }
        for (const print::Line& m : sheet.marks) {
            device.drawLine(QPointF(m.a.x * pt, m.a.y * pt), QPointF(m.b.x * pt, m.b.y * pt), kFramePt * pt);
        }
        if (!sheet.label.empty()) {
            const RectF area = paper.printableFor(sheet.landscape);
            device.drawLabel(
                QRectF(area.x0 * pt, (area.y1 - 2 * kLabelPt) * pt, area.width() * pt, 2 * kLabelPt * pt),
                QString::fromStdString(sheet.label), kLabelPt * pt);
        }
    };

    bool landscape = plan.sheets[static_cast<std::size_t>(order.front())].landscape;
    try {
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (cancel_->flag.load()) {
                break;
            }
            const int index = order[i];
            if (index >= 0) {
                landscape = plan.sheets[static_cast<std::size_t>(index)].landscape;
            }
            if (i > 0 && !device.newSheet(landscape)) {
                error_ = device.errorString();
                device.abort();
                return PrintStatus::Failed;
            }
            if (index >= 0) {
                drawSheet(plan.sheets[static_cast<std::size_t>(index)]);
            }
            ++printed_;
            if (progress) {
                progress(static_cast<int>(i) + 1, static_cast<int>(order.size()));
            }
        }
    } catch (const MuError& e) {
        device.abort();
        if (e.kind() == ErrorKind::Aborted || cancel_->flag.load()) {
            spdlog::info("print: cancelled");
            return PrintStatus::Cancelled;
        }
        error_ = QString::fromUtf8(e.what());
        spdlog::warn("print: {}", e.what());
        return PrintStatus::Failed;
    } catch (const std::exception& e) {
        device.abort();
        error_ = QString::fromUtf8(e.what());
        spdlog::warn("print: {}", e.what());
        return PrintStatus::Failed;
    }
    if (cancel_->flag.load()) {
        device.abort();
        spdlog::info("print: cancelled");
        return PrintStatus::Cancelled;
    }
    if (!device.end()) {
        error_ = device.errorString();
        spdlog::warn("print: {}", error_.toStdString());
        return PrintStatus::Failed;
    }
    return PrintStatus::Done;
}

PrintStatus printFileSilently(const QString& path, const QString& printer, QString* error) {
    const auto fail = [&](PrintStatus status, const QString& message) {
        spdlog::warn("print {}: {}", path.toStdString(), message.toStdString());
        if (error != nullptr) {
            *error = message;
        }
        return status;
    };
    DocumentSession session;
    if (!session.open(path)) {
        return fail(PrintStatus::Failed, QStringLiteral("cannot open: %1").arg(session.errorString()));
    }
    const Settings st;
    PrintSettings s;
    s.title = QFileInfo(path).fileName();
    s.maxDpi = st.get(settings::kPrintQuality);
    s.layout.size = printScaleFromName(st.get(settings::kPrintScale));
    std::unique_ptr<PrintDevice> device = PrintDevices::open(printer);
    std::unique_ptr<QPrinter> qprinter;
    if (device == nullptr) {
        const QPrinterInfo info =
            printer.isEmpty() ? QPrinterInfo::defaultPrinter() : QPrinterInfo::printerInfo(printer);
        if (info.isNull()) {
            return fail(PrintStatus::Failed, printer.isEmpty()
                                                 ? QStringLiteral("no default printer")
                                                 : QStringLiteral("printer \"%1\" not found").arg(printer));
        }
        qprinter = std::make_unique<QPrinter>(info, QPrinter::HighResolution);
        device = makePrinterDevice(*qprinter);
    }
    PrintJob job(session, s);
    const PrintStatus status = job.run(*device);
    session.close();
    switch (status) {
        case PrintStatus::Done:
            spdlog::info("print {}: {} sheets to \"{}\"", path.toStdString(), job.sheetsPrinted(),
                         device->name().toStdString());
            return status;
        case PrintStatus::Denied:
            return fail(status, QStringLiteral("printing is not allowed by the document"));
        case PrintStatus::NothingToPrint:
        case PrintStatus::RangeError:
            return fail(status, QStringLiteral("nothing to print"));
        case PrintStatus::Cancelled:
            return fail(status, QStringLiteral("cancelled"));
        case PrintStatus::Failed:
            break;
    }
    return fail(status, job.errorString());
}

}  // namespace ot
