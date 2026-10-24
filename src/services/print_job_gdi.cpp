// Печать через GDI без окна драйвера (план 2.6; эталон 3.3 «печать в файл»; create-15 — замена): DEVMODE
// принтера (OpenPrinterW + DocumentPropertiesW), CreateDCW(L"WINSPOOL", принтер) и StartDocW с lpszOutput —
// «Microsoft Print to PDF» пишет PDF в заданный файл без своего окна «Сохранить как». Бумага и поля —
// GetDeviceCaps (PHYSICALWIDTH/HEIGHT, PHYSICALOFFSETX/Y, HORZRES/VERTRES, LOGPIXELSX) обеих ориентаций;
// ориентация листа — ResetDCW между EndPage и StartPage. Растр — StretchDIBits снизу вверх (24 бита BGR или
// 8 бит с серой палитрой: перевёрнутые сверху вниз DIB понимают не все драйверы). Копии печатает программа.
#include <QImage>

#include <spdlog/spdlog.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "services/print_job.h"

#include <windows.h>   // NOMINMAX, WIN32_LEAN_AND_MEAN — у целей (CMakeLists.txt)
#include <winspool.h>  // OpenPrinterW, DocumentPropertiesW: WIN32_LEAN_AND_MEAN его не включает

#pragma comment(lib, "winspool.lib")

namespace ot {

namespace {

struct Caps {
    int dpi = 0;
    print::SizeF size;  // лист в пунктах
    RectF printable;    // пункты
    int offsetX = 0;    // начало области печати, пиксели устройства
    int offsetY = 0;
};

Caps capsOf(HDC dc) {
    Caps c;
    c.dpi = GetDeviceCaps(dc, LOGPIXELSX);
    const float k = c.dpi > 0 ? 72.0f / static_cast<float>(c.dpi) : 1.0f;
    const int w = GetDeviceCaps(dc, PHYSICALWIDTH);
    const int h = GetDeviceCaps(dc, PHYSICALHEIGHT);
    c.offsetX = GetDeviceCaps(dc, PHYSICALOFFSETX);
    c.offsetY = GetDeviceCaps(dc, PHYSICALOFFSETY);
    const int hr = GetDeviceCaps(dc, HORZRES);
    const int vr = GetDeviceCaps(dc, VERTRES);
    c.size = {static_cast<float>(w) * k, static_cast<float>(h) * k};
    c.printable = {static_cast<float>(c.offsetX) * k, static_cast<float>(c.offsetY) * k,
                   static_cast<float>(c.offsetX + hr) * k, static_cast<float>(c.offsetY + vr) * k};
    return c;
}

class GdiDevice final : public PrintDevice {
public:
    GdiDevice(std::wstring printer, std::wstring output, std::vector<std::byte> devmode)
        : printer_(std::move(printer)), output_(std::move(output)), devmode_(std::move(devmode)) {
        portrait_ = measure(false);
        landscape_ = measure(true);
    }
    ~GdiDevice() override {
        if (dc_ != nullptr) {
            AbortDoc(dc_);
            DeleteDC(dc_);
        }
    }

    bool valid() const { return portrait_.dpi > 0; }

    QString name() const override { return QString::fromStdWString(printer_); }
    print::Paper paper() const override {
        print::Paper p;
        p.size = portrait_.size;
        p.printable = portrait_.printable;
        p.printableLandscape = landscape_.printable;
        return p;
    }
    int resolution() const override { return portrait_.dpi; }

    bool begin(const PrintDeviceOptions& options, bool landscape) override {
        DEVMODEW* dm = devmode();
        dm->dmFields |= DM_ORIENTATION | DM_COPIES | DM_DUPLEX;
        dm->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        dm->dmCopies = 1;
        dm->dmDuplex = options.duplex == DuplexMode::LongSide    ? DMDUP_VERTICAL
                       : options.duplex == DuplexMode::ShortSide ? DMDUP_HORIZONTAL
                                                                 : DMDUP_SIMPLEX;
        dc_ = CreateDCW(L"WINSPOOL", printer_.c_str(), nullptr, dm);
        if (dc_ == nullptr) {
            error_ = QStringLiteral("CreateDC failed (%1)").arg(GetLastError());
            return false;
        }
        const std::wstring title = options.title.toStdWString();
        DOCINFOW di{};
        di.cbSize = sizeof(di);
        di.lpszDocName = title.c_str();
        di.lpszOutput = output_.empty() ? nullptr : output_.c_str();
        if (StartDocW(dc_, &di) <= 0) {
            error_ = QStringLiteral("StartDoc failed (%1)").arg(GetLastError());
            DeleteDC(dc_);
            dc_ = nullptr;
            return false;
        }
        landscapeNow_ = landscape;
        return StartPage(dc_) > 0;
    }

    bool newSheet(bool landscape) override {
        if (EndPage(dc_) <= 0) {
            error_ = QStringLiteral("EndPage failed (%1)").arg(GetLastError());
            return false;
        }
        if (landscape != landscapeNow_) {
            DEVMODEW* dm = devmode();
            dm->dmFields |= DM_ORIENTATION;
            dm->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            ResetDCW(dc_, dm);
            landscapeNow_ = landscape;
        }
        return StartPage(dc_) > 0;
    }

    void drawImage(const QRectF& target, const QImage& image) override {
        const bool gray = image.format() == QImage::Format_Grayscale8;
        // Снизу вверх; 24 бита — порядок BGR.
        const QImage dib =
            (gray ? image : image.convertToFormat(QImage::Format_RGB888).rgbSwapped()).flipped(Qt::Vertical);
        struct {
            BITMAPINFOHEADER header;
            RGBQUAD palette[256];
        } bmi{};
        bmi.header.biSize = sizeof(BITMAPINFOHEADER);
        bmi.header.biWidth = dib.width();
        bmi.header.biHeight = dib.height();
        bmi.header.biPlanes = 1;
        bmi.header.biBitCount = gray ? 8 : 24;
        bmi.header.biCompression = BI_RGB;
        if (gray) {
            bmi.header.biClrUsed = 256;
            for (int i = 0; i < 256; ++i) {
                bmi.palette[i] = {static_cast<BYTE>(i), static_cast<BYTE>(i), static_cast<BYTE>(i), 0};
            }
        }
        const Caps& c = current();
        const QRect r = target.translated(-c.offsetX, -c.offsetY).toAlignedRect();
        StretchDIBits(dc_, r.x(), r.y(), r.width(), r.height(), 0, 0, dib.width(), dib.height(),
                      dib.constBits(), reinterpret_cast<const BITMAPINFO*>(&bmi), DIB_RGB_COLORS, SRCCOPY);
    }

    void drawLine(const QPointF& a, const QPointF& b, qreal width) override {
        const Caps& c = current();
        HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(std::lround(width))), RGB(0, 0, 0));
        HGDIOBJ old = SelectObject(dc_, pen);
        MoveToEx(dc_, static_cast<int>(std::lround(a.x())) - c.offsetX,
                 static_cast<int>(std::lround(a.y())) - c.offsetY, nullptr);
        LineTo(dc_, static_cast<int>(std::lround(b.x())) - c.offsetX,
               static_cast<int>(std::lround(b.y())) - c.offsetY);
        SelectObject(dc_, old);
        DeleteObject(pen);
    }

    void drawFrame(const QRectF& rect, qreal width) override {
        const Caps& c = current();
        HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(std::lround(width))), RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(dc_, pen);
        HGDIOBJ oldBrush = SelectObject(dc_, GetStockObject(NULL_BRUSH));
        const QRect r = rect.translated(-c.offsetX, -c.offsetY).toRect();
        Rectangle(dc_, r.left(), r.top(), r.right() + 1, r.bottom() + 1);
        SelectObject(dc_, oldBrush);
        SelectObject(dc_, oldPen);
        DeleteObject(pen);
    }

    void drawLabel(const QRectF& rect, const QString& text, qreal pixelSize) override {
        const Caps& c = current();
        HFONT font = CreateFontW(-std::max(1, static_cast<int>(std::lround(pixelSize))), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ old = SelectObject(dc_, font);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(0, 0, 0));
        const std::wstring w = text.toStdWString();
        TextOutW(dc_, static_cast<int>(rect.x()) - c.offsetX, static_cast<int>(rect.y()) - c.offsetY,
                 w.c_str(), static_cast<int>(w.size()));
        SelectObject(dc_, old);
        DeleteObject(font);
    }

    bool end() override {
        bool ok = EndPage(dc_) > 0;
        ok = EndDoc(dc_) > 0 && ok;
        if (!ok) {
            error_ = QStringLiteral("EndDoc failed (%1)").arg(GetLastError());
        }
        DeleteDC(dc_);
        dc_ = nullptr;
        return ok;
    }

    void abort() override {
        if (dc_ != nullptr) {
            AbortDoc(dc_);
            DeleteDC(dc_);
            dc_ = nullptr;
        }
    }

    QString errorString() const override { return error_; }

private:
    DEVMODEW* devmode() { return reinterpret_cast<DEVMODEW*>(devmode_.data()); }
    const Caps& current() const { return landscapeNow_ ? landscape_ : portrait_; }

    Caps measure(bool landscape) {
        DEVMODEW* dm = devmode();
        dm->dmFields |= DM_ORIENTATION;
        dm->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
        HDC ic = CreateICW(L"WINSPOOL", printer_.c_str(), nullptr, dm);
        if (ic == nullptr) {
            return {};
        }
        Caps c = capsOf(ic);
        DeleteDC(ic);
        if (landscape) {
            // Лист в пунктах — всегда книжный (Paper); у альбомного меняются только поля.
            std::swap(c.size.width, c.size.height);
        }
        return c;
    }

    std::wstring printer_;
    std::wstring output_;
    std::vector<std::byte> devmode_;
    Caps portrait_;
    Caps landscape_;
    HDC dc_ = nullptr;
    bool landscapeNow_ = false;
    QString error_;
};

}  // namespace

std::unique_ptr<PrintDevice> makeGdiDevice(const QString& printer, const QString& output) {
    std::wstring name = printer.toStdWString();
    HANDLE handle = nullptr;
    if (!OpenPrinterW(name.data(), &handle, nullptr)) {
        spdlog::warn("print: printer \"{}\" not found", printer.toStdString());
        return nullptr;
    }
    const LONG size = DocumentPropertiesW(nullptr, handle, name.data(), nullptr, nullptr, 0);
    std::vector<std::byte> devmode(size > 0 ? static_cast<std::size_t>(size) : 0);
    const bool ok = size > 0 && DocumentPropertiesW(nullptr, handle, name.data(),
                                                    reinterpret_cast<DEVMODEW*>(devmode.data()), nullptr,
                                                    DM_OUT_BUFFER) == IDOK;
    ClosePrinter(handle);
    if (!ok) {
        spdlog::warn("print: no DEVMODE for \"{}\"", printer.toStdString());
        return nullptr;
    }
    auto device = std::make_unique<GdiDevice>(std::move(name), output.toStdWString(), std::move(devmode));
    if (!device->valid()) {
        spdlog::warn("print: no device context for \"{}\"", printer.toStdString());
        return nullptr;
    }
    return device;
}

}  // namespace ot
