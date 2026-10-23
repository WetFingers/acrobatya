#include "ui/theme/icons.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <tuple>

#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

constexpr auto kPrefix = ":/icons/";

struct CacheKey {
    QString name;
    QRgb color;
    int size;
    int dpr100;  // dpr × 100

    bool operator==(const CacheKey& o) const {
        return std::tie(name, color, size, dpr100) == std::tie(o.name, o.color, o.size, o.dpr100);
    }
};

size_t qHash(const CacheKey& k, size_t seed = 0) {
    return qHashMulti(seed, k.name, k.color, k.size, k.dpr100);
}

struct Store {
    QHash<QString, QByteArray> sources;  // исходные SVG по имени
    QHash<CacheKey, QPixmap> pixmaps;
    bool hooked = false;
};

Store& store() {
    static Store s;
    if (!s.hooked) {
        s.hooked = true;
        // Смена темы — новые цвета; старые растры больше не нужны.
        QObject::connect(&Theme::instance(), &Theme::themeChanged, [] { store().pixmaps.clear(); });
    }
    return s;
}

const QByteArray* source(const QString& name) {
    Store& s = store();
    auto it = s.sources.find(name);
    if (it == s.sources.end()) {
        QFile f(QLatin1String(kPrefix) + name + QStringLiteral(".svg"));
        if (!f.open(QIODevice::ReadOnly)) {
#ifndef QT_NO_DEBUG
            qFatal(
                "Icons: unknown icon \"%s\" (add it to src/ui/theme/icon_names.txt and run "
                "tools/copy_icons.py)",
                qPrintable(name));
#else
            qWarning("Icons: unknown icon \"%s\"", qPrintable(name));
            return nullptr;
#endif
        }
        it = s.sources.insert(name, f.readAll());
    }
    return &it.value();
}

qreal effectiveDpr(qreal dpr) {
    if (dpr > 0) {
        return dpr;
    }
    return qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
}

QPixmap render(const QByteArray& svg, int size, qreal dpr) {
    const int px = qRound(size * dpr);
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, px, px));
    painter.end();
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    return pm;
}

// QIcon, который при каждой отрисовке берёт цвета текущей темы (кэш Icons — по цвету).
class ThemedIconEngine final : public QIconEngine {
public:
    ThemedIconEngine(QString name, IconTone tone) : name_(std::move(name)), tone_(tone) {}

    QIconEngine* clone() const override { return new ThemedIconEngine(name_, tone_); }
    QString key() const override { return QStringLiteral("ot-themed"); }
    QString iconName() override { return name_; }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        return Icons::get(name_, toneFor(mode, state), qMin(size.width(), size.height()), scale);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal dpr = painter->device() != nullptr ? painter->device()->devicePixelRatio() : 1.0;
        const int side = qMin(rect.width(), rect.height());
        const QPixmap pm = Icons::get(name_, toneFor(mode, state), side, dpr);
        painter->drawPixmap(
            QRect(rect.x() + (rect.width() - side) / 2, rect.y() + (rect.height() - side) / 2, side, side),
            pm);
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
        const int side = qMin(size.width(), size.height());
        return QSize(side, side);
    }

private:
    IconTone toneFor(QIcon::Mode mode, QIcon::State state) const {
        if (mode == QIcon::Disabled) {
            return IconTone::Disabled;
        }
        if (state == QIcon::On && tone_ == IconTone::Text) {
            return IconTone::Accent;
        }
        return tone_;
    }

    QString name_;
    IconTone tone_;
};

}  // namespace

QColor Icons::toneColor(IconTone tone) {
    const Palette& p = Theme::colors();
    switch (tone) {
        case IconTone::Muted:
            return p.textMuted.color();
        case IconTone::Accent:
            return p.accent.color();
        case IconTone::OnAccent:
            return p.onAccent.color();
        case IconTone::Danger:
            return p.danger.color();
        case IconTone::Warn:
            return p.warn.color();
        case IconTone::Ok:
            return p.ok.color();
        case IconTone::Info:
            return p.info.color();
        case IconTone::Form:
            return p.form.color();
        case IconTone::Disabled:
            return mix(p.surface.color(), p.textMuted.color(), 0.65);
        case IconTone::Text:
            break;
    }
    return p.text.color();
}

QByteArray Icons::svgData(const QString& name, const QColor& color) {
    const QByteArray* src = source(name);
    if (src == nullptr) {
        return {};
    }
    QByteArray svg = *src;
    svg.replace("stroke-width=\"2\"", "stroke-width=\"1.75\"");
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    return svg;
}

QPixmap Icons::get(const QString& name, const QColor& color, int size, qreal dpr) {
    dpr = effectiveDpr(dpr);
    const CacheKey key{name, color.rgba(), size, qRound(dpr * 100)};
    Store& s = store();
    if (auto it = s.pixmaps.constFind(key); it != s.pixmaps.constEnd()) {
        return it.value();
    }
    const QByteArray svg = svgData(name, color);
    if (svg.isEmpty() || size <= 0) {
        return {};
    }
    QPixmap pm = render(svg, size, dpr);
    s.pixmaps.insert(key, pm);
    return pm;
}

QPixmap Icons::get(const QString& name, IconTone tone, int size, qreal dpr) {
    return get(name, toneColor(tone), size, dpr);
}

QIcon Icons::icon(const QString& name, IconTone tone) {
    if (source(name) == nullptr) {
        return {};
    }
    return QIcon(new ThemedIconEngine(name, tone));
}

bool Icons::exists(const QString& name) {
    return !name.isEmpty() && QFile::exists(QLatin1String(kPrefix) + name + QStringLiteral(".svg"));
}

QStringList Icons::names() {
    QStringList list;
    for (const QString& file :
         QDir(QStringLiteral(":/icons")).entryList({QStringLiteral("*.svg")}, QDir::Files)) {
        list << file.chopped(4);
    }
    return list;
}

int Icons::cacheSize() { return static_cast<int>(store().pixmaps.size()); }

void Icons::clearCache() { store().pixmaps.clear(); }

}  // namespace ot::ui
