// «Подпись» и «Инициалы» Fill & Sign (план 5.3, п. 1; эталон 3.5, 6.3 «Fill & Sign: Подпись/Инициалы
// (Набрать/Нарисовать/Картинка, цвет, сохранённые)», вопрос 12.9; sign-01, sign-02): три способа —
//   Набрать — имя из Установок (инициалы — первые буквы) системным рукописным шрифтом Segoe Script или Segoe
//     Print (services/signature_image.h typedImage);
//   Нарисовать — мышью в поле SignaturePad: сглаженные кривые → PNG с альфой (strokesToImage), «Очистить»;
//   Картинка — PNG/JPEG с диска: белый фон по порогу яркости уходит в прозрачность, «Сохранить исходный цвет»
//     (иначе — цвет чернил);
// цвет чернил — чёрные, синие, красные (kSignatureInkColors); «Сохранить подпись» —
// services/saved_signatures.h (<данные>/signatures/{signature,initials}.png). «Применить» — картинка
// (обрезана по чернилам, ARGB32) в сигнале captured; инструмент ставит её штампом. Диалог — open()
// (окно-модальный, WA_DeleteOnClose у capture), тесты зовут setFileChooser и accept без exec().
#pragma once

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

#include "services/saved_signatures.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QToolButton;

namespace ot::ui {

class SegmentedBar;

// Поле «Нарисовать»: бумага с линией-подсказкой, штрихи мышью (точки в px виджета).
class SignaturePad : public QWidget {
    Q_OBJECT
public:
    static constexpr double kPenWidth = 2.5;  // толщина пера, px поля

    explicit SignaturePad(QWidget* parent = nullptr);

    const std::vector<std::vector<QPointF>>& strokes() const noexcept { return strokes_; }
    bool isEmpty() const noexcept { return strokes_.empty(); }
    void clear();
    // Штрих точками поля (сцены; мышь даёт то же).
    void addStroke(std::vector<QPointF> stroke);
    void setInkColor(const QColor& color);
    // Картинка штрихов (в scale раз крупнее поля), обрезана по чернилам; пусто — пустая.
    QImage image(double scale = 3.0) const;

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<std::vector<QPointF>> strokes_;
    QColor ink_ = Qt::black;
    bool drawing_ = false;
};

class SignatureCaptureDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Type, Draw, Image };
    using FileChooser = std::function<QString(QWidget* parent)>;

    explicit SignatureCaptureDialog(fillsign::SignatureKind kind, QWidget* parent = nullptr);

    // Окно-модальный диалог (WA_DeleteOnClose): результат — сигнал captured.
    static SignatureCaptureDialog* capture(fillsign::SignatureKind kind, QWidget* parent);

    fillsign::SignatureKind kind() const noexcept { return kind_; }
    Mode mode() const noexcept { return mode_; }
    void setMode(Mode mode);
    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    // Картинка для «Картинка» (диалог «Выбрать…» зовёт её же); не читается — false.
    bool loadImageFile(const QString& path);

    // Картинка по текущему способу (ARGB32, обрезана); нечего ставить — пустая.
    QImage result() const;
    QColor inkColor() const;
    void setInkColor(const QColor& color);

    SegmentedBar* modeBar() const { return modeBar_; }
    QLineEdit* nameEdit() const { return name_; }
    QComboBox* fontBox() const { return font_; }
    SignaturePad* pad() const { return pad_; }
    QPushButton* clearButton() const { return clear_; }
    QPushButton* browseButton() const { return browse_; }
    QSlider* thresholdSlider() const { return threshold_; }
    QCheckBox* keepColorBox() const { return keepColor_; }
    QCheckBox* saveBox() const { return save_; }
    // Кнопки цвета чернил по порядку kSignatureInkColors.
    QToolButton* inkButton(int index) const;
    QPushButton* applyButton() const { return apply_; }
    QLabel* preview() const { return preview_; }

public slots:
    void accept() override;

signals:
    // «Применить»: картинка подписи (и, если отмечено, уже сохранена).
    void captured(ot::fillsign::SignatureKind kind, const QImage& image);

private:
    QWidget* buildType();
    QWidget* buildDraw();
    QWidget* buildImage();
    void refresh();
    void browse();

    fillsign::SignatureKind kind_;
    Mode mode_ = Mode::Type;
    FileChooser chooser_;
    QImage source_;  // картинка «Картинка» как прочитана
    SegmentedBar* modeBar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLineEdit* name_ = nullptr;
    QComboBox* font_ = nullptr;
    QLabel* typePreview_ = nullptr;
    SignaturePad* pad_ = nullptr;
    QPushButton* clear_ = nullptr;
    QPushButton* browse_ = nullptr;
    QSlider* threshold_ = nullptr;
    QCheckBox* keepColor_ = nullptr;
    QLabel* preview_ = nullptr;
    std::vector<QToolButton*> inks_;
    QColor ink_;
    QCheckBox* save_ = nullptr;
    QPushButton* apply_ = nullptr;
};

}  // namespace ot::ui
