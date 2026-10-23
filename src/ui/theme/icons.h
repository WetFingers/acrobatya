// Иконки Lucide 1.50 (ISC, эталон 6.5): SVG из ресурса :/icons/<имя>.svg (assets/icons, список —
// icon_names.txt), QSvgRenderer с подстановкой stroke="<токен>" и stroke-width="1.75", кэш по (имя, цвет,
// размер, dpr). Icons::icon даёт QIcon, который при каждой отрисовке берёт цвета текущей темы — смена темы
// перекрашивает без переустановки иконок; Icons::get — готовый QPixmap. Неизвестное имя — qFatal в Debug.
#pragma once

#include <QByteArray>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <string_view>

namespace ot::ui {

enum class IconTone { Text, Muted, Accent, OnAccent, Danger, Warn, Ok, Info, Form, Disabled };

class Icons {
public:
    // size — сторона в логических px (20 в панелях, 16 в строках); dpr = 0 — qApp->devicePixelRatio().
    static QPixmap get(const QString& name, IconTone tone = IconTone::Text, int size = 20, qreal dpr = 0);
    static QPixmap get(const QString& name, const QColor& color, int size, qreal dpr = 0);
    // QIcon по теме: Normal/Active — tone, On (нажатая) — accent, Disabled — приглушённый.
    static QIcon icon(const QString& name, IconTone tone = IconTone::Text);

    static bool exists(const QString& name);
    static QStringList names();                                           // все встроенные
    static QColor toneColor(IconTone tone);                               // цвет тона в текущей теме
    static QByteArray svgData(const QString& name, const QColor& color);  // SVG после подстановки
    static int cacheSize();
    static void clearCache();
};

// Соответствие эталона 6.5: id инструмента (папка src/ui/tools/<id>/) или панели → имя Lucide.
struct IconMapping {
    const char* id;
    const char* icon;
};

inline constexpr IconMapping kToolIcons[] = {
    {"edit", "pencil-line"},
    {"organize", "layout-grid"},
    {"comment", "message-square"},
    {"fillsign", "pen-line"},
    {"prepareform", "clipboard-list"},
    {"create", "file-plus"},
    {"combine", "files"},
    {"export", "file-output"},
    {"protect", "lock"},
    {"compress", "shrink"},
    {"certificate", "badge-check"},
    {"scan", "scan-text"},
    {"print", "printer"},
    {"measure", "ruler"},
    {"accessibility", "accessibility"},
    {"javascript", "braces"},
};

inline constexpr IconMapping kPaneIcons[] = {
    {"thumbnails", "images"}, {"bookmarks", "bookmark"},    {"comments", "message-square-text"},
    {"layers", "layers"},     {"attachments", "paperclip"}, {"signatures", "file-signature"},
    {"fields", "list-tree"},  {"destinations", "locate"},
};

// Имя Lucide по id инструмента или панели; неизвестный id — nullptr.
constexpr const char* toolIconName(std::string_view id) {
    for (const auto& m : kToolIcons) {
        if (id == m.id) {
            return m.icon;
        }
    }
    for (const auto& m : kPaneIcons) {
        if (id == m.id) {
            return m.icon;
        }
    }
    return nullptr;
}

}  // namespace ot::ui
