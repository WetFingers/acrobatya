// «Восстановить несохранённые изменения?» (план 2.5; эталон 4.5, 6.3 «Восстановление при старте»; view-27):
// при запуске, если автосохранение оставило записи (программа закрылась, не сохранив правки), — список файлов
// с флажками и временем автосохранения; «Восстановить» — отмеченные открываются с правками и историей отмены
// (файл не сохранён), «Не восстанавливать» — записи удаляются, «Позже» — остаются до следующего запуска.
#pragma once

#include <QDialog>

#include <vector>

#include "core/doc/autosave.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace ot::ui {

class RecoverDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { Later, Recover, Discard };

    explicit RecoverDialog(std::vector<AutosaveEntry> entries, QWidget* parent = nullptr);

    QListWidget* list() const { return list_; }
    QPushButton* recoverButton() const { return recover_; }
    QPushButton* discardButton() const { return discard_; }
    QPushButton* laterButton() const { return later_; }

    const std::vector<AutosaveEntry>& entries() const { return entries_; }
    // Отмеченные (для «Восстановить»).
    std::vector<AutosaveEntry> selectedEntries() const;
    Choice choice() const { return choice_; }

signals:
    void recoverRequested(const std::vector<ot::AutosaveEntry>& entries);
    void discardRequested(const std::vector<ot::AutosaveEntry>& entries);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void finish(Choice choice);

    std::vector<AutosaveEntry> entries_;
    QLabel* text_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* recover_ = nullptr;
    QPushButton* discard_ = nullptr;
    QPushButton* later_ = nullptr;
    Choice choice_ = Choice::Later;
};

}  // namespace ot::ui
