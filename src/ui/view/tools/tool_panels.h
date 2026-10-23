// Панели инструментов «Лупа» и «Панорамирование и масштаб» (эталон 3.1 «Масштаб»; view-05): плавающие окна в
// правом верхнем углу PageView (дети вида — не прокручиваются с документом), размер — токены toolWindow*,
// цвета — тема. Растр в них рисует задача документа (services/clipboard_io.h); панель только показывает и
// сообщает о действиях — вид двигает инструмент.
#pragma once

#include <QFrame>
#include <QImage>
#include <QPointF>
#include <QRectF>

class QLabel;
class QSlider;

namespace ot::ui {

class PanelHeader;
class ToolButton;

// Картинка, вписанная по центру (с рамкой области — для миниатюры).
class ToolImage : public QWidget {
    Q_OBJECT
public:
    explicit ToolImage(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    const QImage& image() const noexcept { return image_; }
    // Рамка области в долях картинки (0…1); пустая — нет.
    void setFrame(const QRectF& fraction);
    QRectF frame() const noexcept { return frame_; }
    // Где картинка внутри виджета.
    QRectF imageRect() const;
    void setInteractive(bool on) noexcept { interactive_ = on; }

signals:
    // Нажатие или перетаскивание по картинке: точка в долях картинки.
    void pointed(QPointF fraction);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QImage image_;
    QRectF frame_;
    bool interactive_ = false;
};

class ToolPanel : public QFrame {
    Q_OBJECT
public:
    ToolPanel(const QString& title, QWidget* host);
    // В правый верхний угол вьюпорта хоста.
    void place();

signals:
    void closeRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void addBody(QWidget* body);
    void addFooter(QWidget* footer);

private:
    void applyMetrics();
    PanelHeader* header_ = nullptr;
};

class LoupePanel : public ToolPanel {
    Q_OBJECT
public:
    explicit LoupePanel(QWidget* host);
    void setImage(const QImage& image) { image_->setImage(image); }
    const QImage& image() const noexcept { return image_->image(); }
    // Размер картинки лупы (логические px).
    QSize imageSize() const;
    void setPercent(int percent);  // без сигнала
    int percent() const;
    QSlider* slider() const noexcept { return slider_; }

signals:
    void percentChanged(int percent);

private:
    ToolImage* image_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* label_ = nullptr;
};

class PanZoomPanel : public ToolPanel {
    Q_OBJECT
public:
    explicit PanZoomPanel(QWidget* host);
    void setThumbnail(const QImage& image) { thumb_->setImage(image); }
    const QImage& thumbnail() const noexcept { return thumb_->image(); }
    QSize thumbnailSize() const;
    void setFrame(const QRectF& fraction) { thumb_->setFrame(fraction); }
    QRectF frame() const noexcept { return thumb_->frame(); }
    void setZoomText(const QString& text);
    ToolImage* thumbnailWidget() const noexcept { return thumb_; }

signals:
    void pointed(QPointF fraction);
    void previousPage();
    void nextPage();
    void zoomIn();
    void zoomOut();

private:
    ToolImage* thumb_ = nullptr;
    QLabel* zoom_ = nullptr;
};

}  // namespace ot::ui
