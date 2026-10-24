#include "ui/dialogs/save_as_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "ui/theme/theme.h"

namespace ot::ui {

SaveAsDialog::SaveAsDialog(const QString& suggested, bool isSigned, QWidget* parent)
    : QDialog(parent), isSigned_(isSigned) {
    setObjectName(QStringLiteral("saveAsDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(m.grid * 2);
    grid->setVerticalSpacing(m.grid * 2);
    pathLabel_ = new QLabel(this);
    grid->addWidget(pathLabel_, 0, 0);
    path_ = new QLineEdit(this);
    path_->setObjectName(QStringLiteral("saveAsPath"));
    pathLabel_->setBuddy(path_);
    grid->addWidget(path_, 0, 1);
    browse_ = new QPushButton(this);
    connect(browse_, &QPushButton::clicked, this, &SaveAsDialog::browse);
    grid->addWidget(browse_, 0, 2);
    modeLabel_ = new QLabel(this);
    grid->addWidget(modeLabel_, 1, 0, Qt::AlignTop);
    auto* modes = new QVBoxLayout();
    modes->setSpacing(m.grid);
    auto_ = new QRadioButton(this);
    incremental_ = new QRadioButton(this);
    full_ = new QRadioButton(this);
    auto* group = new QButtonGroup(this);
    for (QRadioButton* b : {auto_, incremental_, full_}) {
        group->addButton(b);
        modes->addWidget(b);
        connect(b, &QRadioButton::toggled, this, &SaveAsDialog::updateEnabled);
    }
    auto_->setChecked(true);
    auto* extra = new QVBoxLayout();
    extra->setContentsMargins(m.grid * 6, 0, 0, 0);
    objstms_ = new QCheckBox(this);
    objstms_->setChecked(true);
    images_ = new QCheckBox(this);
    images_->setChecked(true);
    subset_ = new QCheckBox(this);
    for (QCheckBox* b : {objstms_, images_, subset_}) {
        extra->addWidget(b);
    }
    modes->addLayout(extra);
    grid->addLayout(modes, 1, 1, 1, 2);
    grid->setColumnStretch(1, 1);
    column->addLayout(grid);
    signedNote_ = new QLabel(this);
    signedNote_->setObjectName(QStringLiteral("saveAsSignedNote"));
    signedNote_->setWordWrap(true);
    signedNote_->setVisible(isSigned_);
    column->addWidget(signedNote_);
    column->addStretch(1);
    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Save);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    ok_->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!path().isEmpty()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_, &QLineEdit::textChanged, this, &SaveAsDialog::updateEnabled);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth * 2 / 3);
    setPath(suggested);
    retranslate();
    updateEnabled();
}

QString SaveAsDialog::path() const {
    QString p = QDir::fromNativeSeparators(path_->text().trimmed());
    if (!p.isEmpty() && QFileInfo(p).suffix().isEmpty()) {
        p += QStringLiteral(".pdf");
    }
    return p;
}

void SaveAsDialog::setPath(const QString& path) { path_->setText(QDir::toNativeSeparators(path)); }

SaveOptions SaveAsDialog::options() const {
    SaveOptions o;
    if (incremental_->isChecked()) {
        o.mode = SaveMode::Incremental;
    } else if (full_->isChecked()) {
        o.mode = SaveMode::Full;
        o.objectStreams = objstms_->isChecked();
        o.compressImages = images_->isChecked();
        o.subsetFonts = subset_->isChecked();
    } else {
        o.subsetFonts = subset_->isChecked();  // применяется, если программа решит писать полностью
    }
    return o;
}

void SaveAsDialog::browse() {
    const QString current = path();
    const QString file = chooser_ ? chooser_(this, current)
                                  : QFileDialog::getSaveFileName(this, tr("Сохранить как"), current,
                                                                 tr("Документы PDF (*.pdf)"));
    if (!file.isEmpty()) {
        setPath(file);
    }
}

void SaveAsDialog::updateEnabled() {
    if (ok_ == nullptr) {
        return;  // конструктор: переключатели уже есть, флажков и кнопок ещё нет
    }
    const bool full = full_->isChecked();
    objstms_->setEnabled(full);
    images_->setEnabled(full);
    subset_->setEnabled(!incremental_->isChecked());
    ok_->setEnabled(!path_->text().trimmed().isEmpty());
}

void SaveAsDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void SaveAsDialog::retranslate() {
    setWindowTitle(tr("Сохранить как"));
    pathLabel_->setText(tr("&Файл:"));
    browse_->setText(tr("&Обзор…"));
    modeLabel_->setText(tr("Запись:"));
    auto_->setText(tr("Как решит программа (изменения дописываются, если это возможно)"));
    incremental_->setText(tr("&Дописать изменения в конец файла"));
    full_->setText(tr("&Записать файл заново"));
    objstms_->setText(tr("Потоки &объектов (меньше размер файла)"));
    images_->setText(tr("Сжимать &изображения"));
    subset_->setText(tr("Встраивать &подмножества шрифтов"));
    signedNote_->setText(
        tr("Документ подписан. Полная запись сделает подписи недействительными; чтобы подписи "
           "остались, выберите «Как решит программа» или «Дописать изменения»."));
    ok_->setText(tr("&Сохранить"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
