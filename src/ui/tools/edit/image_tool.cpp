// Картинки режима «Редактировать» (план 3.4, п. 3; эталон 3.9 «Инструмент»; edit-06, edit-07, create-32):
// рамки картинок страницы (пунктир textMuted), выбранная — accent с восемью маркерами; перетаскивание —
// перемещение, маркер — масштаб (предпросмотр рамкой, правка — по отпусканию одним шагом истории); правая
// кнопка — меню; Del, Ctrl+C/X/V (буфер: PNG/JPEG как есть + своя пометка с размером на странице — вставка в
// другую страницу или документ); обрезка — рамкой с маркерами (W n в единицах картинки, без перекодирования);
// «Заменить…», «Добавить изображение», «Экспортировать…» — файловый диалог (в тестах — setFileChooser);
// «Редактировать в…» — временный файл, ShellLauncher::openFile, QFileSystemWatcher: сохранили — замена.
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

#include "services/document_session.h"
#include "services/shell_launcher.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

const QString kImageMime = QStringLiteral("application/x-acrobatya-image");
const QString kImageSizeMime = QStringLiteral("application/x-acrobatya-image-size");
constexpr float kMinImage = 4;  // сторона картинки не меньше, пт

EditMode::FileChooser& chooser() {
    static EditMode::FileChooser fn;
    return fn;
}

QRectF toQ(const RectF& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

RectF normalized(float x0, float y0, float x1, float y1) {
    return RectF{std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
}

// Рамка после перетаскивания маркера grip на (dx, dy) пунктов страницы.
RectF gripped(const RectF& r, EditMode::Grip grip, float dx, float dy) {
    float x0 = r.x0, y0 = r.y0, x1 = r.x1, y1 = r.y1;
    switch (grip) {
        case EditMode::Grip::Inside:
            return RectF{x0 + dx, y0 + dy, x1 + dx, y1 + dy};
        case EditMode::Grip::TopLeft:
            x0 += dx;
            y0 += dy;
            break;
        case EditMode::Grip::Top:
            y0 += dy;
            break;
        case EditMode::Grip::TopRight:
            x1 += dx;
            y0 += dy;
            break;
        case EditMode::Grip::Right:
            x1 += dx;
            break;
        case EditMode::Grip::BottomRight:
            x1 += dx;
            y1 += dy;
            break;
        case EditMode::Grip::Bottom:
            y1 += dy;
            break;
        case EditMode::Grip::BottomLeft:
            x0 += dx;
            y1 += dy;
            break;
        case EditMode::Grip::Left:
            x0 += dx;
            break;
        case EditMode::Grip::None:
            break;
    }
    RectF out = normalized(x0, y0, x1, y1);
    if (out.width() < kMinImage) {
        out.x1 = out.x0 + kMinImage;
    }
    if (out.height() < kMinImage) {
        out.y1 = out.y0 + kMinImage;
    }
    return out;
}

}  // namespace

void EditMode::setFileChooser(FileChooser fn) { chooser() = std::move(fn); }

// ---------------------------------------------------------------- выбор

const edit::PageImage* EditMode::image(ImageRef ref) const {
    const auto it = pages_.constFind(ref.page);
    if (!ref.valid() || it == pages_.constEnd() || ref.index >= static_cast<int>(it->images.size())) {
        return nullptr;
    }
    return &it->images[static_cast<std::size_t>(ref.index)];
}

EditMode::ImageRef EditMode::imageAt(QPointF viewportPoint) const {
    const int page = view_.pageAtViewport(viewportPoint);
    const auto it = pages_.constFind(page);
    if (page < 0 || it == pages_.constEnd()) {
        return {};
    }
    const QPointF p = view_.viewportToPage(page, viewportPoint);
    const int index = edit::imageAt(it->images, PointF{static_cast<float>(p.x()), static_cast<float>(p.y())});
    return index >= 0 ? ImageRef{page, index} : ImageRef{};
}

void EditMode::selectImage(ImageRef ref) {
    if (ref == selected_) {
        return;
    }
    if (ref.valid()) {
        leaveBlock();
    }
    crop_.reset();
    selected_ = ref;
    imageDrag_ = Grip::None;
    view_.viewport()->update();
    emit imageSelectionChanged();
}

void EditMode::clearImageSelection() { selectImage({}); }

RectF EditMode::draggedImageRect() const {
    const edit::PageImage* img = image(selected_);
    if (img == nullptr) {
        return RectF::none();
    }
    const RectF base = crop_ ? *crop_ : img->bbox;
    if (imageDrag_ == Grip::None) {
        return base;
    }
    const auto dx = static_cast<float>(imageNow_.x() - imagePress_.x());
    const auto dy = static_cast<float>(imageNow_.y() - imagePress_.y());
    RectF r = gripped(base, imageDrag_, dx, dy);
    if (crop_) {
        // Обрезка — внутри картинки.
        r = RectF{std::max(r.x0, img->bbox.x0), std::max(r.y0, img->bbox.y0), std::min(r.x1, img->bbox.x1),
                  std::min(r.y1, img->bbox.y1)};
    }
    return r;
}

std::optional<QRectF> EditMode::imageFrame() const {
    if (!selected_.valid() || image(selected_) == nullptr ||
        view_.pageViewportRect(selected_.page).isEmpty()) {
        return std::nullopt;
    }
    return view_.pageTransform(selected_.page).mapRect(toQ(draggedImageRect()));
}

EditMode::Grip EditMode::gripAt(QPointF viewportPoint) const {
    const std::optional<QRectF> frame = imageFrame();
    if (!frame || locked()) {
        return Grip::None;
    }
    const double h = Theme::sizes().handle;
    const QRectF r = *frame;
    const std::pair<Grip, QPointF> grips[] = {
        {Grip::TopLeft, r.topLeft()},         {Grip::Top, QPointF(r.center().x(), r.top())},
        {Grip::TopRight, r.topRight()},       {Grip::Right, QPointF(r.right(), r.center().y())},
        {Grip::BottomRight, r.bottomRight()}, {Grip::Bottom, QPointF(r.center().x(), r.bottom())},
        {Grip::BottomLeft, r.bottomLeft()},   {Grip::Left, QPointF(r.left(), r.center().y())},
    };
    for (const auto& [grip, at] : grips) {
        if (std::abs(viewportPoint.x() - at.x()) <= h && std::abs(viewportPoint.y() - at.y()) <= h) {
            return grip;
        }
    }
    return r.contains(viewportPoint) ? Grip::Inside : Grip::None;
}

// ---------------------------------------------------------------- мышь и клавиши

bool EditMode::imagePress(QMouseEvent* event) {
    const QPointF pos = event->position();
    if (event->button() == Qt::RightButton) {
        const ImageRef hit = imageAt(pos);
        if (!hit.valid() || locked()) {
            return false;
        }
        selectImage(hit);
        QMenu* menu = imageMenu(view_.viewport());
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(view_.viewport()->mapToGlobal(pos.toPoint()));
        return true;
    }
    if (event->button() != Qt::LeftButton || !selected_.valid() || locked()) {
        return false;
    }
    const Grip grip = gripAt(pos);
    if (grip == Grip::None) {
        if (crop_) {
            applyCrop();  // щелчок вне рамки обрезки — применить
            return true;
        }
        return false;
    }
    if (crop_ && grip == Grip::Inside) {
        return true;
    }
    imageDrag_ = grip;
    imagePress_ = view_.viewportToPage(selected_.page, pos);
    imageNow_ = imagePress_;
    view_.setFocus();
    return true;
}

bool EditMode::imageMove(QMouseEvent* event) {
    if (imageDrag_ == Grip::None || !selected_.valid()) {
        return false;
    }
    if ((event->buttons() & Qt::LeftButton) == 0) {
        imageDrag_ = Grip::None;
        return false;
    }
    imageNow_ = view_.viewportToPage(selected_.page, event->position());
    view_.viewport()->update();
    return true;
}

bool EditMode::imageRelease(QMouseEvent* event) {
    if (imageDrag_ == Grip::None || event->button() != Qt::LeftButton) {
        return false;
    }
    imageNow_ = view_.viewportToPage(selected_.page, event->position());
    const RectF r = draggedImageRect();
    const Grip grip = imageDrag_;
    imageDrag_ = Grip::None;
    const QPointF d = imageNow_ - imagePress_;
    const double minMove = 1.0 / std::max(0.01f, view_.zoom());
    if (std::abs(d.x()) < minMove && std::abs(d.y()) < minMove) {
        view_.viewport()->update();
        return true;  // щелчок — только выбор
    }
    if (crop_) {
        crop_ = r;
        view_.viewport()->update();
        return true;
    }
    DocumentSession* session = view_.document();
    if (session != nullptr) {
        if (grip == Grip::Inside) {
            imageEdited(editing::moveImage(*session, selected_.page, selected_.index,
                                           PointF{static_cast<float>(d.x()), static_cast<float>(d.y())}),
                        tr("Не удалось переместить изображение"));
        } else {
            imageEdited(editing::setImageRect(*session, selected_.page, selected_.index, r),
                        tr("Не удалось изменить размер изображения"));
        }
    }
    view_.viewport()->update();
    return true;
}

bool EditMode::imageKey(QKeyEvent* event) {
    const Qt::KeyboardModifiers m = event->modifiers() & ~Qt::KeypadModifier;
    const int k = event->key();
    if (m == Qt::ControlModifier && k == Qt::Key_V) {
        return pasteImage();
    }
    if (!selected_.valid()) {
        return false;
    }
    if (m == Qt::NoModifier && k == Qt::Key_Delete) {
        deleteImage();
        return true;
    }
    if (m == Qt::NoModifier && (k == Qt::Key_Return || k == Qt::Key_Enter) && crop_) {
        applyCrop();
        return true;
    }
    if (m == Qt::ControlModifier && (k == Qt::Key_C || k == Qt::Key_X)) {
        copyImage(k == Qt::Key_X);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- правки

void EditMode::imageEdited(QFuture<void> future, const QString& failure) {
    future.then(this, [this, failure](QFuture<void> done) {
        try {
            done.waitForFinished();
        } catch (const std::exception&) {
            emit editFailed(failure);
        }
    });
}

void EditMode::rotateImage(float degrees) {
    DocumentSession* session = view_.document();
    if (session != nullptr && selected_.valid() && !locked()) {
        crop_.reset();
        imageEdited(editing::rotateImage(*session, selected_.page, selected_.index, degrees),
                    tr("Не удалось повернуть изображение"));
    }
}

void EditMode::flipImage(bool horizontal) {
    DocumentSession* session = view_.document();
    if (session != nullptr && selected_.valid() && !locked()) {
        crop_.reset();
        imageEdited(editing::flipImage(*session, selected_.page, selected_.index, horizontal),
                    tr("Не удалось отразить изображение"));
    }
}

void EditMode::deleteImage() {
    DocumentSession* session = view_.document();
    if (session != nullptr && selected_.valid() && !locked()) {
        const ImageRef ref = selected_;
        clearImageSelection();
        imageEdited(editing::removeImage(*session, ref.page, ref.index),
                    tr("Не удалось удалить изображение"));
    }
}

void EditMode::startCrop() {
    if (const edit::PageImage* img = image(selected_); img != nullptr && !locked()) {
        crop_ = img->bbox;
        view_.viewport()->update();
        emit imageSelectionChanged();
    }
}

void EditMode::setCropRect(const RectF& pageRect) {
    if (crop_) {
        crop_ = pageRect;
        view_.viewport()->update();
    }
}

void EditMode::applyCrop() {
    DocumentSession* session = view_.document();
    if (!crop_ || session == nullptr || !selected_.valid()) {
        crop_.reset();
        return;
    }
    const RectF r = *crop_;
    crop_.reset();
    imageEdited(editing::cropImage(*session, selected_.page, selected_.index, r),
                tr("Не удалось обрезать изображение"));
    emit imageSelectionChanged();
    view_.viewport()->update();
}

void EditMode::cancelCrop() {
    crop_.reset();
    view_.viewport()->update();
    emit imageSelectionChanged();
}

void EditMode::replaceImage(const QByteArray& bytes) {
    DocumentSession* session = view_.document();
    if (session == nullptr || !selected_.valid() || bytes.isEmpty() || locked()) {
        return;
    }
    if (edit::probeImage({bytes.constData(), static_cast<std::size_t>(bytes.size())}).width <= 0) {
        emit editFailed(tr("Файл не похож на изображение"));
        return;
    }
    imageEdited(editing::replaceImage(*session, selected_.page, selected_.index, bytes),
                tr("Не удалось заменить изображение"));
}

namespace {

QString chooseFile(QWidget* parent, bool save, const QString& suggested) {
    if (chooser()) {
        return chooser()(parent, save, suggested);
    }
    const QString filter = QCoreApplication::translate(
        "ot::ui::EditMode", "Изображения (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)");
    return save ? QFileDialog::getSaveFileName(
                      parent, QCoreApplication::translate("ot::ui::EditMode", "Экспорт изображения"),
                      suggested)
                : QFileDialog::getOpenFileName(
                      parent, QCoreApplication::translate("ot::ui::EditMode", "Выберите изображение"),
                      QString(), filter);
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

void EditMode::replaceImageFromFile() {
    if (!selected_.valid()) {
        return;
    }
    const QString path = chooseFile(view_.window(), false, QString());
    if (!path.isEmpty()) {
        replaceImage(readAll(path));
    }
}

void EditMode::addImage(const QByteArray& bytes, int page, std::optional<RectF> rect) {
    DocumentSession* session = view_.document();
    if (session == nullptr || page < 0 || bytes.isEmpty() || locked()) {
        return;
    }
    const edit::ImageData probe =
        edit::probeImage({bytes.constData(), static_cast<std::size_t>(bytes.size())});
    if (probe.width <= 0 || probe.height <= 0) {
        emit editFailed(tr("Файл не похож на изображение"));
        return;
    }
    if (!rect) {
        // По центру видимой части страницы, 72 dpi, не больше половины страницы.
        const QSizeF size = view_.pageSizePoints(page);
        const QRectF visible = view_.pageTransform(page)
                                   .inverted()
                                   .mapRect(QRectF(view_.viewport()->rect()))
                                   .intersected(QRectF(QPointF(0, 0), size));
        const QPointF c = visible.isEmpty() ? QPointF(size.width() / 2, size.height() / 2) : visible.center();
        double w = probe.width;
        double h = probe.height;
        const double k = std::min({1.0, size.width() / 2 / w, size.height() / 2 / h});
        w *= k;
        h *= k;
        rect = RectF{static_cast<float>(c.x() - w / 2), static_cast<float>(c.y() - h / 2),
                     static_cast<float>(c.x() + w / 2), static_cast<float>(c.y() + h / 2)};
    }
    leaveBlock();
    editing::addImage(*session, page, bytes, *rect).then(this, [this, page](QFuture<int> done) {
        try {
            const int index = done.result();
            // Выбор — когда придут новые картинки страницы.
            QObject::connect(
                this, &EditMode::contentLoaded, this,
                [this, page, index](int loaded) {
                    if (loaded == page) {
                        selectImage({page, index});
                    }
                },
                Qt::SingleShotConnection);
            requestContent(page);
        } catch (const std::exception&) {
            emit editFailed(tr("Не удалось добавить изображение"));
        }
    });
}

void EditMode::addImageFromFile() {
    const QString path = chooseFile(view_.window(), false, QString());
    if (!path.isEmpty()) {
        addImage(readAll(path), view_.currentPage());
    }
}

void EditMode::copyImage(bool cut) {
    DocumentSession* session = view_.document();
    const edit::PageImage* img = image(selected_);
    if (session == nullptr || img == nullptr) {
        return;
    }
    const QSizeF size(img->bbox.width(), img->bbox.height());
    const ImageRef ref = selected_;
    editing::exportImage(*session, ref.page, ref.index)
        .then(this, [this, size, cut, ref](QFuture<edit::ImageData> done) {
            try {
                const edit::ImageData data = done.result();
                const QByteArray bytes(data.bytes.data(), static_cast<qsizetype>(data.bytes.size()));
                auto* mime = new QMimeData();
                mime->setData(kImageMime, bytes);
                mime->setData(kImageSizeMime,
                              QByteArray::number(size.width()) + ' ' + QByteArray::number(size.height()));
                const QImage picture = QImage::fromData(bytes);
                if (!picture.isNull()) {
                    mime->setImageData(picture);
                }
                QGuiApplication::clipboard()->setMimeData(mime);
                if (cut && selected_ == ref) {
                    deleteImage();
                }
            } catch (const std::exception&) {
                emit editFailed(tr("Не удалось скопировать изображение"));
            }
        });
}

bool EditMode::pasteImage() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr || locked()) {
        return false;
    }
    QByteArray bytes = mime->data(kImageMime);
    std::optional<RectF> rect;
    if (bytes.isEmpty() && mime->hasImage()) {
        const QImage picture = qvariant_cast<QImage>(mime->imageData());
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
    }
    if (bytes.isEmpty()) {
        return false;
    }
    const QList<QByteArray> size = mime->data(kImageSizeMime).split(' ');
    const int page = view_.currentPage();
    if (size.size() == 2) {
        // Тот же размер на странице — по центру видимой части.
        const float w = size[0].toFloat();
        const float h = size[1].toFloat();
        const QSizeF pageSize = view_.pageSizePoints(page);
        const QRectF visible = view_.pageTransform(page)
                                   .inverted()
                                   .mapRect(QRectF(view_.viewport()->rect()))
                                   .intersected(QRectF(QPointF(0, 0), pageSize));
        const QPointF c =
            visible.isEmpty() ? QPointF(pageSize.width() / 2, pageSize.height() / 2) : visible.center();
        if (w > 0 && h > 0) {
            rect = RectF{static_cast<float>(c.x()) - w / 2, static_cast<float>(c.y()) - h / 2,
                         static_cast<float>(c.x()) + w / 2, static_cast<float>(c.y()) + h / 2};
        }
    }
    addImage(bytes, page, rect);
    return true;
}

void EditMode::exportImage() {
    DocumentSession* session = view_.document();
    if (session == nullptr || !selected_.valid()) {
        return;
    }
    const ImageRef ref = selected_;
    editing::exportImage(*session, ref.page, ref.index)
        .then(this, [this, ref](QFuture<edit::ImageData> done) {
            edit::ImageData data;
            try {
                data = done.result();
            } catch (const std::exception&) {
                emit editFailed(tr("Не удалось экспортировать изображение"));
                return;
            }
            const QString suggested = tr("изображение-%1-%2").arg(ref.page + 1).arg(ref.index + 1) +
                                      QLatin1Char('.') + QString::fromStdString(data.format);
            QString path = chooseFile(view_.window(), true, suggested);
            if (path.isEmpty()) {
                return;
            }
            const QString ext = QLatin1Char('.') + QString::fromStdString(data.format);
            if (!path.endsWith(ext, Qt::CaseInsensitive) &&
                !(data.format == "jpg" && path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))) {
                path += ext;  // формат — как в документе (DCT — JPEG как есть)
            }
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly) ||
                f.write(data.bytes.data(), static_cast<qint64>(data.bytes.size())) !=
                    static_cast<qint64>(data.bytes.size())) {
                emit editFailed(tr("Не удалось записать файл"));
            }
        });
}

void EditMode::editImageExternally() {
    DocumentSession* session = view_.document();
    if (session == nullptr || !selected_.valid() || locked()) {
        return;
    }
    const ImageRef ref = selected_;
    editing::exportImage(*session, ref.page, ref.index)
        .then(this, [this, ref, id = session->id()](QFuture<edit::ImageData> done) {
            edit::ImageData data;
            try {
                data = done.result();
            } catch (const std::exception&) {
                emit editFailed(tr("Не удалось подготовить изображение"));
                return;
            }
            QDir dir(QDir::tempPath());
            dir.mkpath(QStringLiteral("AcroBatya-images"));
            dir.cd(QStringLiteral("AcroBatya-images"));
            const QString path = dir.filePath(QStringLiteral("image-%1-%2-%3.%4")
                                                  .arg(id)
                                                  .arg(ref.page + 1)
                                                  .arg(ref.index + 1)
                                                  .arg(QString::fromStdString(data.format)));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                emit editFailed(tr("Не удалось записать временный файл"));
                return;
            }
            f.write(data.bytes.data(), static_cast<qint64>(data.bytes.size()));
            f.close();
            if (!externalPath_.isEmpty()) {
                watcher_.removePath(externalPath_);
            }
            externalPath_ = path;
            externalImage_ = ref;
            watcher_.addPath(path);
            QObject::disconnect(&watcher_, nullptr, this, nullptr);
            QObject::connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &EditMode::externalChanged);
            if (!ShellLauncher::instance().openFile(path)) {
                emit editFailed(tr("Нет программы для файлов «%1»").arg(QFileInfo(path).suffix()));
            }
        });
}

void EditMode::externalChanged(const QString& path) {
    if (path != externalPath_) {
        return;
    }
    // Редакторы пишут через временный файл и переименование — путь возвращается под наблюдение.
    QTimer::singleShot(150, this, [this, path] {
        if (!watcher_.files().contains(path) && QFileInfo::exists(path)) {
            watcher_.addPath(path);
        }
        const QByteArray bytes = readAll(path);
        DocumentSession* session = view_.document();
        if (bytes.isEmpty() || session == nullptr || !externalImage_.valid() || locked()) {
            return;
        }
        if (edit::probeImage({bytes.constData(), static_cast<std::size_t>(bytes.size())}).width <= 0) {
            return;  // файл ещё пишется
        }
        imageEdited(editing::replaceImage(*session, externalImage_.page, externalImage_.index, bytes),
                    tr("Не удалось заменить изображение"));
    });
}

QMenu* EditMode::imageMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("imageMenu"));
    auto add = [menu](const char* name, const QString& text, const QString& icon, auto fn) {
        QAction* a = menu->addAction(text);
        a->setObjectName(QLatin1String(name));
        if (!icon.isEmpty()) {
            a->setIcon(Icons::icon(icon));
        }
        QObject::connect(a, &QAction::triggered, menu, fn);
        return a;
    };
    add("imageRotateCw", tr("Повернуть по часовой стрелке"), QStringLiteral("rotate-cw"),
        [this] { rotateImage(90); });
    add("imageRotateCcw", tr("Повернуть против часовой стрелки"), QStringLiteral("rotate-ccw"),
        [this] { rotateImage(-90); });
    // Произвольный угол (план 3.4, «Осторожно»: поворот 90°/произвольный; ядро — ImageOps::rotate): по
    // часовой — положительный, как у кнопок.
    add("imageRotateAngle", tr("Повернуть на угол…"), QString(), [this, parent] {
        bool ok = false;
        const double angle = QInputDialog::getDouble(parent, tr("Повернуть изображение"), tr("Угол, °:"),
                                                     15.0, -360.0, 360.0, 1, &ok);
        if (ok && angle != 0.0) {
            rotateImage(static_cast<float>(angle));
        }
    });
    add("imageFlipH", tr("Отразить по горизонтали"), QStringLiteral("flip-horizontal-2"),
        [this] { flipImage(true); });
    add("imageFlipV", tr("Отразить по вертикали"), QStringLiteral("flip-vertical-2"),
        [this] { flipImage(false); });
    add("imageCrop", tr("Обрезать изображение"), QStringLiteral("crop"), [this] { startCrop(); });
    menu->addSeparator();
    add("imageReplace", tr("Заменить изображение…"), QString(), [this] { replaceImageFromFile(); });
    add("imageEditIn", tr("Редактировать в…"), QStringLiteral("square-pen"),
        [this] { editImageExternally(); });
    add("imageExport", tr("Экспортировать изображение…"), QString(), [this] { exportImage(); });
    menu->addSeparator();
    add("imageCut", tr("Вырезать"), QString(), [this] { copyImage(true); });
    add("imageCopy", tr("Копировать"), QString(), [this] { copyImage(false); });
    add("imagePaste", tr("Вставить"), QString(), [this] { pasteImage(); });
    add("imageDelete", tr("Удалить"), QStringLiteral("trash-2"), [this] { deleteImage(); });
    return menu;
}

// ---------------------------------------------------------------- слой

void EditMode::paintImages(QPainter& painter) const {
    const Palette& colors = Theme::colors();
    for (const int page : view_.visiblePages()) {
        const auto it = pages_.constFind(page);
        if (it == pages_.constEnd()) {
            continue;
        }
        const QTransform t = view_.pageTransform(page);
        for (const edit::PageImage& img : it->images) {
            if (selected_.page == page && selected_.index == img.index) {
                continue;
            }
            QPen pen(colors.textMuted.color(), 1, Qt::DotLine);
            pen.setCosmetic(true);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(t.mapRect(toQ(img.bbox)).adjusted(-1, -1, 1, 1));
        }
    }
    const std::optional<QRectF> frame = imageFrame();
    if (!frame) {
        return;
    }
    const edit::PageImage* img = image(selected_);
    if (crop_ && img != nullptr) {
        // Вне рамки обрезки — затемнение поверх картинки.
        const QRectF full = view_.pageTransform(selected_.page).mapRect(toQ(img->bbox));
        QPainterPath outside;
        outside.addRect(full);
        QPainterPath inside;
        inside.addRect(*frame);
        QColor dim = colors.shadow.color();
        dim.setAlpha(std::max(dim.alpha(), 110));
        painter.fillPath(outside.subtracted(inside), dim);
    }
    painter.setPen(QPen(colors.accent.color(), crop_ ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(*frame);
    const double h = Theme::sizes().handle;
    painter.setBrush(colors.surface.color());
    painter.setPen(QPen(colors.accent.color(), 1));
    const QRectF r = *frame;
    for (const QPointF c :
         {r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(), QPointF(r.right(), r.center().y()),
          r.bottomRight(), QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
          QPointF(r.left(), r.center().y())}) {
        painter.drawRect(QRectF(c.x() - h / 2, c.y() - h / 2, h, h));
    }
}

}  // namespace ot::ui
