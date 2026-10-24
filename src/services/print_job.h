// Задание печати (план 2.6; эталон 3.3; view-37, view-43; create-15 — замена: системный «Microsoft Print to
// PDF»): план (какие страницы — core/print/page_set.h, как на листах — core/print/imposition.h) и печать
// листов на устройство — растр MuPDF полосами (core/print/band_renderer.h) в точные прямоугольники
// устройства.
//   * Устройства (PrintDevice): принтер Windows — QPrinter (HighResolution, setFullPage(true), ориентация
//   листа
//     перед newPage(), копии/подбор, дуплекс, лоток); PDF-файл — QPdfWriter (тот же QPdfEngine, что у
//     QPrinter::PdfFormat, без перебора принтеров системы: суррогат печати в тестах, результат читает MuPDF);
//     «тихо в файл» через драйвер принтера — GDI CreateDC(L"WINSPOOL", принтер) + StartDoc с lpszOutput
//     (print_job_gdi.cpp; «Microsoft Print to PDF» без своего окна «Сохранить как»); FakePrintDevice — листы
//     растрами в памяти (тесты, `/pt` без принтера).
//   * Подмена для тестов и процессов exe (как FakeShellLauncher): ScopedPrintDevices — свой список принтеров
//   и
//     фабрика устройств; переменная OTTISK_FAKE_PRINTER=<каталог> — любой «принтер» печатает в
//     <каталог>/<принтер>-<N>.pdf (QPdfWriter) и строку в <каталог>/jobs.jsonl. Живой принтер в тестах не
//     открывается никогда.
//   * DPI растра — DPI устройства, но не выше «качества» (print/quality, 300: у «Microsoft Print to PDF» 1200
//   dpi);
//     «Печать как изображение» (view-43) — свой dpi; без права «печать высокого качества» — не выше 150; без
//     права «печать» — отказ (Denied). Печать — всегда растром (ответ на вопрос 12 (10)).
//   * Поток: run() блокирует вызывающий поток (не поток документа!): display lists — задачами сессии, растр и
//     рисование — здесь (QPainter на QPrinter можно вне GUI-потока). Отмена — cancel() из любого потока:
//     fz_cookie.abort текущего растра и QPrinter::abort() / AbortDoc.
#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "core/print/imposition.h"
#include "core/print/page_set.h"

class QPrinter;

namespace ot {

class DocumentSession;
struct OpenSummary;

enum class DuplexMode { None, LongSide, ShortSide };

// Параметры устройства на всё задание.
struct PrintDeviceOptions {
    QString title;  // имя задания в очереди печати
    int copies = 1;
    bool collate = true;
    DuplexMode duplex = DuplexMode::None;
    int paperSource = -1;  // QPrinter::PaperSource; -1 — как в драйвере
};

// Устройство листов: координаты — пиксели устройства от левого верхнего угла бумаги (resolution() dpi).
class PrintDevice {
public:
    virtual ~PrintDevice() = default;
    virtual QString name() const = 0;
    // Бумага и области печати обеих ориентаций (до begin).
    virtual print::Paper paper() const = 0;
    virtual int resolution() const = 0;
    // Устройство само печатает копии (драйвер): тогда задание идёт один раз.
    virtual bool supportsCopies() const { return false; }
    virtual bool supportsDuplex() const { return false; }
    virtual bool begin(const PrintDeviceOptions& options, bool landscape) = 0;
    // Следующий лист (между листами; ориентация — для него).
    virtual bool newSheet(bool landscape) = 0;
    virtual void drawImage(const QRectF& target, const QImage& image) = 0;
    virtual void drawLine(const QPointF& a, const QPointF& b, qreal width) = 0;
    virtual void drawFrame(const QRectF& rect, qreal width) = 0;
    virtual void drawLabel(const QRectF& rect, const QString& text, qreal pixelSize) = 0;
    virtual bool end() = 0;
    virtual void abort() = 0;
    virtual QString errorString() const { return {}; }
};

// Принтер Windows через QPrinter (не владеет им): бумага — pageLayout() с полями minimumMargins обеих
// ориентаций.
std::unique_ptr<PrintDevice> makePrinterDevice(QPrinter& printer);
// PDF-файл через QPdfWriter: бумага pageSize (пункты, книжно), resolution dpi, без полей.
std::unique_ptr<PrintDevice> makePdfDevice(const QString& path,
                                           print::SizeF pageSize = {595.2756f, 841.8898f},
                                           int resolution = 300);
// GDI: принтер printer, вывод в файл output (пусто — на принтер) без окна драйвера. Нет принтера — nullptr.
std::unique_ptr<PrintDevice> makeGdiDevice(const QString& printer, const QString& output = {});

// Листы растрами в памяти (resolution dpi): тесты и `/pt` под OTTISK_FAKE_PRINTER.
class FakePrintDevice : public PrintDevice {
public:
    struct Sheet {
        bool landscape = false;
        QImage image;  // лист целиком, белый фон
        int images = 0;
        QList<QSize> imageSizes;  // растры полос (пиксели) — по ним видно dpi
        int lines = 0;
        int frames = 0;
        QStringList labels;
    };

    explicit FakePrintDevice(QString name = QStringLiteral("Fake Printer"), int resolution = 36,
                             print::Paper paper = print::Paper::a4(12));
    QString name() const override { return name_; }
    print::Paper paper() const override { return paper_; }
    int resolution() const override { return resolution_; }
    bool supportsDuplex() const override { return duplex_; }
    void setSupportsDuplex(bool on) { duplex_ = on; }
    bool begin(const PrintDeviceOptions& options, bool landscape) override;
    bool newSheet(bool landscape) override;
    void drawImage(const QRectF& target, const QImage& image) override;
    void drawLine(const QPointF& a, const QPointF& b, qreal width) override;
    void drawFrame(const QRectF& rect, qreal width) override;
    void drawLabel(const QRectF& rect, const QString& text, qreal pixelSize) override;
    bool end() override;
    void abort() override { aborted = true; }

    PrintDeviceOptions options;
    QList<Sheet> sheets;
    bool begun = false;
    bool ended = false;
    bool aborted = false;

private:
    void addSheet(bool landscape);

    QString name_;
    int resolution_;
    print::Paper paper_;
    bool duplex_ = false;
};

// Принтеры и устройства с подменой (тесты, OTTISK_FAKE_PRINTER).
class PrintDevices {
public:
    using Factory =
        std::function<std::unique_ptr<PrintDevice>(const QString& printer, const QString& output)>;

    // Принтеры системы (QPrinterInfo) или подменённые; принтер по умолчанию.
    static QStringList printers();
    static QString defaultPrinter();
    // Поддерживает ли принтер дуплекс (QPrinterInfo::supportedDuplexModes; подмена — её устройство,
    // OTTISK_FAKE_PRINTER — нет).
    static bool supportsDuplex(const QString& printer);
    // Подмена действует (тест или OTTISK_FAKE_PRINTER).
    static bool overridden();
    // Бумага принтера для плана и предпросмотра (подмена — бумага её устройства, OTTISK_FAKE_PRINTER — A4 без
    // полей, иначе — QPrinter этого принтера: размер по умолчанию и поля minimumMargins). GUI-поток.
    static print::Paper paperFor(const QString& printer);
    // Устройство для печати на printer (вывод в output — «печать в файл»): подмена — фабрикой; иначе
    // «Microsoft Print to PDF» с output — GDI, прочее — nullptr (окно печатает своим QPrinter через
    // makePrinterDevice). Пустое имя — принтер по умолчанию.
    static std::unique_ptr<PrintDevice> open(const QString& printer, const QString& output = {});

private:
    friend class ScopedPrintDevices;
};

// Подмена принтеров на время жизни объекта (тесты): printers — список, первый — по умолчанию; factory —
// устройства.
class ScopedPrintDevices {
public:
    ScopedPrintDevices(QStringList printers, PrintDevices::Factory factory);
    ~ScopedPrintDevices();
    ScopedPrintDevices(const ScopedPrintDevices&) = delete;
    ScopedPrintDevices& operator=(const ScopedPrintDevices&) = delete;
};

// Принтеры сцен --shots и ui-тестов (как FakeShellLauncher: живой принтер не перечисляется и не открывается):
// «HP LaserJet Pro M404» (дуплекс, A4 с полями 12 pt) и «Microsoft Print to PDF» (без дуплекса, без полей) —
// устройства FakePrintDevice в памяти.
QStringList demoPrinters();
PrintDevices::Factory demoPrinterFactory();

// Что печатать и как.
struct PrintSettings {
    print::PageSetOptions pages;
    print::ImposeOptions layout;
    print::PrintContent content = print::PrintContent::DocumentAndMarkups;
    bool grayscale = false;
    bool asImage = false;  // «Печать как изображение» (view-43): растр при imageDpi
    int imageDpi = 300;
    int maxDpi = 300;  // потолок dpi растра (print/quality)
    int copies = 1;
    bool collate = true;
    DuplexMode duplex = DuplexMode::None;
    int paperSource = -1;
    QString title;  // имя задания (имя файла)
};

enum class PrintStatus { Done, Cancelled, Denied, NothingToPrint, RangeError, Failed };

// План: страницы и листы (предпросмотр диалога и печать — одно и то же).
struct PrintPlan {
    PrintStatus status = PrintStatus::Done;  // Done — можно печатать
    std::vector<int> pages;
    std::vector<print::SizeF> sizes;  // размеры всех страниц документа (OpenSummary::pageSizes)
    std::vector<print::Sheet> sheets;
    DuplexMode duplex = DuplexMode::None;  // действующий (буклет «обе стороны» — по короткой стороне)
};

PrintPlan makePrintPlan(const OpenSummary& doc, const PrintSettings& settings, const print::Paper& paper);

// Значение print/scale ("fit", "actual", "shrink") → режим размера.
print::SizeMode printScaleFromName(const QString& name);

// DPI растра для устройства resolution при правах документа (см. выше).
int printDpi(const PrintSettings& settings, int resolution, bool printHighRes);

class PrintJob {
public:
    PrintJob(DocumentSession& session, PrintSettings settings);
    PrintJob(const PrintJob&) = delete;
    PrintJob& operator=(const PrintJob&) = delete;

    // Напечатать на device (блокирует поток; не поток документа). progress(лист, всего) — после каждого листа
    // (с копиями — всех листов задания), из этого же потока.
    PrintStatus run(PrintDevice& device, const std::function<void(int, int)>& progress = {});
    // Предпросмотр листа sheet плана при dpi (диалог «Печать»): тот же растр, что у run(), в QImage листа
    // (белая бумага); sheets — число листов плана (0 — печатать нечего). Нет такого листа — пустой QImage.
    // Блокирует поток (не поток документа); ошибка задачи — исключение.
    QImage preview(const print::Paper& paper, int sheet, int dpi, int* sheets = nullptr);
    // Отменить из любого потока.
    void cancel();
    bool cancelled() const noexcept;
    QString errorString() const { return error_; }
    // Листов отправлено на устройство (с копиями и пустыми оборотами).
    int sheetsPrinted() const noexcept { return printed_; }
    ~PrintJob();

private:
    struct Cancel;  // флаг и fz_cookie текущего растра (MuPDF — только в print_job.cpp)

    DocumentSession& session_;
    PrintSettings settings_;
    std::unique_ptr<Cancel> cancel_;
    QString error_;
    int printed_ = 0;
};

// Печать документа файла на принтер без окна (`/p`, `/pt`, Проводник «Печать»): открыть, все страницы,
// вписать, устройство PrintDevices::open(printer) (нет подмены — QPrinter этого принтера). Ошибка — в журнал
// и в error.
PrintStatus printFileSilently(const QString& path, const QString& printer, QString* error = nullptr);

}  // namespace ot
