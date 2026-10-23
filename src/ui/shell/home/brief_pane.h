// «Краткие сведения» Главной (эталон 3.18): правая панель 280 при выбранной карточке — миниатюра, имя,
// расположение, размер, страниц, изменён, открыт; кнопки «Открыть», «Печать» (появится в 0.2) и «Объединить»
// (в 0.7). Что делают кнопки — решает страница (сигналы).
#pragma once

#include <QWidget>

#include <optional>

#include "services/recent.h"

class QLabel;
class QPushButton;

namespace ot {
class ThumbnailCache;
}

namespace ot::ui {

class PanelHeader;

class BriefPane : public QWidget {
    Q_OBJECT
public:
    explicit BriefPane(ThumbnailCache& thumbs, QWidget* parent = nullptr);

    void setFile(const QString& path, const std::optional<RecentEntry>& entry);
    void clear();
    QString path() const { return path_; }
    QString valueText(const QString& field) const;  // location, size, pages, modified, opened — для тестов

signals:
    void openRequested(const QString& path);
    void printRequested(const QString& path);
    void combineRequested(const QString& path);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void applyMetrics();
    void refreshThumb();

    ThumbnailCache& thumbs_;
    QString path_;
    PanelHeader* header_ = nullptr;
    QLabel* thumb_ = nullptr;
    QLabel* name_ = nullptr;
    QList<QLabel*> captions_;
    QList<QLabel*> values_;
    QPushButton* open_ = nullptr;
    QPushButton* print_ = nullptr;
    QPushButton* combine_ = nullptr;
};

}  // namespace ot::ui
