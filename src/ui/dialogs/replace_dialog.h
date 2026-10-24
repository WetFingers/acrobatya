// «Заменить все» — предпросмотр и отчёт (план 3.4, п. 4; эталон 3.9 «Найти и заменить», 6.3 «Редактирование»;
// edit-05, view-18): список совпадений документа — страница, «было» и «станет» (фрагмент абзаца, совпадение
// и замена — жирным), пропущенные — серым с причиной (через границу абзацев, «только чтение», нет глифов);
// «Заменить все» — замена в документе одним шагом истории, затем отчёт: заменено N, пропущено M (причины),
// шрифты подменены. Немодальный (open()), данные — у FindBar.
#pragma once

#include <QDialog>

#include <vector>

#include "core/edit/replace.h"

class QLabel;
class QPushButton;
class QTreeWidget;

namespace ot::ui {

class ReplaceDialog : public QDialog {
    Q_OBJECT
public:
    ReplaceDialog(const QString& needle, const QString& replacement, std::vector<edit::ReplaceMatch> matches,
                  QWidget* parent = nullptr);

    const std::vector<edit::ReplaceMatch>& matches() const { return matches_; }
    int replaceable() const;
    QTreeWidget* list() const { return list_; }
    QLabel* summary() const { return summary_; }
    QPushButton* replaceButton() const { return replace_; }
    // Отчёт после замены: кнопка «Заменить все» уходит, «Отмена» становится «Закрыть».
    void showReport(const edit::ReplaceReport& report);
    bool hasReport() const { return reported_; }
    static QString skipText(edit::ReplaceSkip skip);
    // Фрагмент с метками \x02…\x03 → HTML с жирным совпадением.
    static QString fragmentHtml(const std::string& text);

signals:
    void replaceAllRequested();

private:
    std::vector<edit::ReplaceMatch> matches_;
    QLabel* summary_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* replace_ = nullptr;
    QPushButton* close_ = nullptr;
    bool reported_ = false;
};

}  // namespace ot::ui
