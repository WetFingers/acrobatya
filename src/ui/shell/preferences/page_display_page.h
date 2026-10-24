// «Установки → Просмотр страницы» (эталон 3.18, 3.1 «Сглаживание и качество»; view-36, pro-47): раскладка и
// масштаб по умолчанию (новые вкладки: DocumentView), сглаживание текста, изображений и векторной графики,
// «Утолщать тонкие линии», предпросмотр наложения «Всегда / Только для PDF/X / Никогда» (стиль рендера —
// ui/view/view_style.h, все вкладки по «ОК»: MainWindow::applyViewStyle), логические номера страниц (строка
// состояния, миниатюры), рамки обрезки, подрезки и выпуска (слой boxes_overlay.h).
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QLabel;
class QComboBox;

namespace ot::ui {

class PropertySheet;

class PageDisplayPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit PageDisplayPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("pageDisplay"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QComboBox* layoutBox() const { return layout_; }
    QComboBox* zoomBox() const { return zoom_; }
    QCheckBox* smoothTextBox() const { return smoothText_; }
    QCheckBox* smoothImagesBox() const { return smoothImages_; }
    QCheckBox* smoothLineArtBox() const { return smoothLineArt_; }
    QCheckBox* thinLinesBox() const { return thinLines_; }
    QCheckBox* pageBoxesBox() const { return boxes_; }
    QComboBox* overprintBox() const { return overprint_; }
    QCheckBox* logicalNumbersBox() const { return logical_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QList<QLabel*> sections_;  // подписи разделов и строк листа — в порядке добавления
    QList<QLabel*> rows_;
    QComboBox* layout_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QCheckBox* smoothText_ = nullptr;
    QCheckBox* smoothImages_ = nullptr;
    QCheckBox* smoothLineArt_ = nullptr;
    QCheckBox* thinLines_ = nullptr;
    QComboBox* overprint_ = nullptr;
    QCheckBox* logical_ = nullptr;
    QCheckBox* boxes_ = nullptr;
};

}  // namespace ot::ui
