#include "services/stamps_catalog.h"

#include <QBuffer>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <mutex>

#include <spdlog/spdlog.h>

#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/mu/error.h"
#include "services/app_info.h"
#include "services/comment_settings.h"
#include "services/sql.h"
#include "services/state_db.h"

namespace ot::stamps {

namespace {

std::uint32_t rgbOf(const QString& hex) {
    bool ok = false;
    const std::uint32_t v = QStringView(hex).mid(1).toUInt(&ok, 16);
    return ok ? v : 0;
}

void sortLocale(QStringList& list) {
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const QString& a, const QString& b) {
        return collator.compare(a, b) < 0;
    });
}

StampEntry userEntry(const QString& root, const QString& category, const QString& stem) {
    StampEntry e;
    e.id = QLatin1String(kUserPrefix) + category + QLatin1Char('/') + stem;
    e.category = category;
    e.caption = stem;
    e.path = QDir(root).filePath(category + QLatin1Char('/') + stem + QStringLiteral(".pdf"));
    e.builtIn = false;
    e.name = "#" + stem.toStdString();
    return e;
}

// Свободное имя файла в папке категории («Имя», «Имя (2)»…).
QString freeStem(const QDir& dir, const QString& stem) {
    QString candidate = stem;
    for (int i = 2; dir.exists(candidate + QStringLiteral(".pdf")); ++i) {
        candidate = QStringLiteral("%1 (%2)").arg(stem).arg(i);
    }
    return candidate;
}

}  // namespace

QString safeName(const QString& name) {
    QString out;
    for (const QChar c : name.trimmed()) {
        out += (c.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(c)) ? QChar(u'_') : c;
    }
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' '))) {
        out.chop(1);
    }
    return out;
}

StampCatalog::StampCatalog() : root_(QDir(app::dataDir()).filePath(QStringLiteral("stamps"))) {}

StampCatalog::StampCatalog(QString userRoot) : root_(std::move(userRoot)) {}

std::vector<StampEntry> StampCatalog::builtIn() const {
    std::vector<StampEntry> out;
    QFile file(QStringLiteral(":/stamps/stamps.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("stamps: built-in catalog is missing");
        return out;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue v : root.value(QLatin1String("stamps")).toArray()) {
        const QJsonObject o = v.toObject();
        StampEntry e;
        e.id = o.value(QLatin1String("id")).toString();
        e.category = e.id.section(QLatin1Char('/'), 0, 0);
        e.caption = o.value(QLatin1String("caption")).toString();
        e.path = QStringLiteral(":/stamps/") + o.value(QLatin1String("file")).toString();
        e.name = o.value(QLatin1String("name")).toString().toStdString();
        e.dynamic = o.value(QLatin1String("dynamic")).toBool();
        const std::uint32_t color = rgbOf(o.value(QLatin1String("color")).toString());
        for (const QJsonValue lv : o.value(QLatin1String("lines")).toArray()) {
            const QJsonObject l = lv.toObject();
            annot::StampLine line;
            line.text = l.value(QLatin1String("text")).toString().toStdString();
            line.x = static_cast<float>(l.value(QLatin1String("x")).toDouble());
            line.y = static_cast<float>(l.value(QLatin1String("y")).toDouble());
            line.size = static_cast<float>(l.value(QLatin1String("size")).toDouble(9));
            line.align = l.value(QLatin1String("align")).toInt(1);
            line.color = color;
            e.lines.push_back(std::move(line));
        }
        if (e.valid()) {
            out.push_back(std::move(e));
        }
    }
    return out;
}

QStringList StampCatalog::userCategories() const {
    QStringList dirs = QDir(root_).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    sortLocale(dirs);
    return dirs;
}

std::vector<StampEntry> StampCatalog::user() const {
    std::vector<StampEntry> out;
    for (const QString& category : userCategories()) {
        QStringList files = QDir(QDir(root_).filePath(category)).entryList({QStringLiteral("*.pdf")}, QDir::Files);
        sortLocale(files);
        for (const QString& f : files) {
            out.push_back(userEntry(root_, category, QFileInfo(f).completeBaseName()));
        }
    }
    return out;
}

std::vector<StampEntry> StampCatalog::all() const {
    std::vector<StampEntry> out = builtIn();
    std::vector<StampEntry> mine = user();
    out.insert(out.end(), std::make_move_iterator(mine.begin()), std::make_move_iterator(mine.end()));
    return out;
}

std::optional<StampEntry> StampCatalog::find(const QString& id) const {
    if (id.startsWith(QLatin1String(kUserPrefix))) {
        const QString rest = id.mid(static_cast<qsizetype>(std::char_traits<char>::length(kUserPrefix)));
        const QString category = rest.section(QLatin1Char('/'), 0, 0);
        const QString stem = rest.section(QLatin1Char('/'), 1);
        StampEntry e = userEntry(root_, category, stem);
        if (QFileInfo::exists(e.path)) {
            return e;
        }
        return std::nullopt;
    }
    for (StampEntry& e : builtIn()) {
        if (e.id == id) {
            return std::move(e);
        }
    }
    return std::nullopt;
}

QByteArray StampCatalog::pdf(const StampEntry& entry) const {
    QFile f(entry.path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

annot::StampSource StampCatalog::source(const StampEntry& entry) const {
    annot::StampSource src;
    const QByteArray bytes = pdf(entry);
    src.pdf.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    src.name = entry.name;
    src.lines = entry.lines;
    return src;
}

annot::StampSize StampCatalog::size(const StampEntry& entry) const {
    const QByteArray bytes = pdf(entry);
    if (bytes.isEmpty()) {
        return {};
    }
    try {
        return annot::stampPageSize(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    } catch (const std::exception& e) {
        spdlog::warn("stamps: cannot read stamp size: {}", e.what());
        return {};
    }
}

std::optional<StampEntry> StampCatalog::createFromPdf(const QByteArray& pdf, int page, const QString& category,
                                                     const QString& name, QString* error) {
    const QString cat = safeName(category);
    const QString stem = safeName(name);
    if (cat.isEmpty() || stem.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("stamp name and category must not be empty");
        }
        return std::nullopt;
    }
    std::string single;
    try {
        single = annot::pageStampPdf(std::string(pdf.constData(), static_cast<std::size_t>(pdf.size())), page);
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = QString::fromUtf8(e.what());
        }
        return std::nullopt;
    }
    QDir dir(root_);
    if (!dir.mkpath(cat)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot create stamp folder");
        }
        return std::nullopt;
    }
    dir.cd(cat);
    const QString free = freeStem(dir, stem);
    QFile out(dir.filePath(free + QStringLiteral(".pdf")));
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(single.data(), static_cast<qint64>(single.size())) != static_cast<qint64>(single.size())) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write stamp file");
        }
        return std::nullopt;
    }
    out.close();
    spdlog::info("stamps: created user stamp {}/{}", cat.toStdString(), free.toStdString());
    return userEntry(root_, cat, free);
}

std::optional<StampEntry> StampCatalog::createFromFile(const QString& file, int page, const QString& category,
                                                      const QString& name, QString* error) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read file");
        }
        return std::nullopt;
    }
    const QByteArray bytes = f.readAll();
    if (QFileInfo(file).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        return createFromPdf(bytes, page, category, name, error);
    }
    std::string pdf;
    try {
        pdf = annot::imageStampPdf(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = QString::fromUtf8(e.what());
        }
        return std::nullopt;
    }
    return createFromPdf(QByteArray(pdf.data(), static_cast<qsizetype>(pdf.size())), 0, category, name, error);
}

std::optional<StampEntry> StampCatalog::createFromImage(const QImage& image, const QString& category,
                                                       const QString& name, QString* error) {
    if (image.isNull()) {
        if (error != nullptr) {
            *error = QStringLiteral("no image");
        }
        return std::nullopt;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    std::string pdf;
    try {
        pdf = annot::imageStampPdf(std::string(png.constData(), static_cast<std::size_t>(png.size())));
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = QString::fromUtf8(e.what());
        }
        return std::nullopt;
    }
    return createFromPdf(QByteArray(pdf.data(), static_cast<qsizetype>(pdf.size())), 0, category, name, error);
}

std::optional<StampEntry> StampCatalog::rename(const QString& id, const QString& name, const QString& category) {
    const std::optional<StampEntry> e = find(id);
    if (!e || e->builtIn) {
        return std::nullopt;
    }
    const QString cat = category.isEmpty() ? e->category : safeName(category);
    const QString stem = safeName(name);
    if (cat.isEmpty() || stem.isEmpty()) {
        return std::nullopt;
    }
    QDir dir(root_);
    if (!dir.mkpath(cat)) {
        return std::nullopt;
    }
    dir.cd(cat);
    const QString free = (cat == e->category && stem == e->caption) ? stem : freeStem(dir, stem);
    if (!QFile::rename(e->path, dir.filePath(free + QStringLiteral(".pdf")))) {
        return std::nullopt;
    }
    QDir(root_).rmdir(e->category);  // пустая прежняя категория уходит
    const bool favorite = isFavorite(id);
    StampEntry renamed = userEntry(root_, cat, free);
    if (favorite) {
        setFavorite(id, false);
        setFavorite(renamed.id, true);
    }
    return renamed;
}

bool StampCatalog::remove(const QString& id) {
    const std::optional<StampEntry> e = find(id);
    if (!e || e->builtIn || !QFile::remove(e->path)) {
        return false;
    }
    QDir(root_).rmdir(e->category);
    setFavorite(id, false);
    return true;
}

QStringList StampCatalog::favorites() const {
    QStringList out;
    StateDb& db = StateDb::shared();
    if (!db.isOpen()) {
        return out;
    }
    std::lock_guard lock(db.mutex());
    SqlStatement st(db.handle(), "SELECT id FROM stamp_favorites ORDER BY added_at, id");
    while (st.step() == SQLITE_ROW) {
        out << st.columnText(0);
    }
    return out;
}

bool StampCatalog::isFavorite(const QString& id) const { return favorites().contains(id); }

void StampCatalog::setFavorite(const QString& id, bool favorite) {
    StateDb& db = StateDb::shared();
    if (!db.isOpen() || id.isEmpty()) {
        return;
    }
    std::lock_guard lock(db.mutex());
    if (favorite) {
        SqlStatement(db.handle(), "INSERT OR IGNORE INTO stamp_favorites(id, added_at) VALUES(?1, ?2)")
            .text(1, id)
            .integer(2, QDateTime::currentMSecsSinceEpoch())
            .run();
    } else {
        SqlStatement(db.handle(), "DELETE FROM stamp_favorites WHERE id = ?1").text(1, id).run();
    }
}

annot::StampValues stampValues(const Settings& settings, const QDateTime& when) {
    const annot::Identity id = comments::identity(settings);
    annot::StampValues v;
    v.name = !id.name.empty() ? id.name : annot::loginName();
    v.title = id.title;
    v.org = id.organization;
    v.date = when.toString(QStringLiteral("dd.MM.yyyy")).toStdString();
    v.time = when.toString(QStringLiteral("HH:mm")).toStdString();
    return v;
}

QImage thumbnail(const QByteArray& pdf, int page, QSize box) {
    if (pdf.isEmpty() || box.isEmpty()) {
        return {};
    }
    try {
        const std::string bytes(pdf.constData(), static_cast<std::size_t>(pdf.size()));
        const annot::StampSize size = annot::stampPageSize(bytes, page);
        if (size.width <= 0 || size.height <= 0) {
            return {};
        }
        const float scale = std::min(static_cast<float>(box.width()) / size.width,
                                     static_cast<float>(box.height()) / size.height);
        auto doc = Document::openOrThrowMemory(bytes);
        const Bitmap bmp = doc->renderPage(page, {.dpi = 72.0f * scale, .annotations = false});
        if (bmp.empty() || bmp.components != 3) {
            return {};
        }
        QImage image(bmp.width, bmp.height, QImage::Format_RGB888);
        for (int y = 0; y < bmp.height; ++y) {
            std::copy_n(bmp.row(y), static_cast<std::size_t>(bmp.width) * 3, image.scanLine(y));
        }
        return image;
    } catch (const std::exception& e) {
        spdlog::warn("stamps: thumbnail failed: {}", e.what());
        return {};
    }
}

}  // namespace ot::stamps
