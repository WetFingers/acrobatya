// ui.save_flow — сохранение, пароль и восстановление в интерфейсе (план 2.5; эталон 3.1 «Открытие»,
// «PDF/A», 4.4, 4.5; security-09, pro-45): диалог пароля даёт три попытки и «запомнить на время сеанса»
// (только память окна); PDF/A-файл открывается только для чтения с полосой «Включить редактирование», после
// неё — нет; восстановленный файл — полоса «сохранение будет полным»; Ctrl+S подписанного документа с тяжёлой
// правкой — предупреждение и «Сохранить как копию» (подписанный файл не меняется); «Сохранить как» с
// параметрами — документ на новом файле; автосохранение после «падения» — «Восстановить несохранённые
// изменения?» открывает правки, закрытие с «Не сохранять» удаляет запись; плашка операции с перезаписью.
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "services/app_info.h"
#include "services/autosave_runner.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/task_context.h"
#include "support/ui_test.h"
#include "ui/dialogs/password_dialog.h"
#include "ui/dialogs/recover_dialog.h"
#include "ui/dialogs/save_as_dialog.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/save_flow.h"
#include "ui/shell/status_bar.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/message_bar.h"

extern "C" {
#include <mupdf/pdf.h>
}

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::MessageKind;
using ot::ui::PasswordDialog;
using ot::ui::SaveFlow;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTest::qWaitForWindowActive(&w);
}

template <class F>
void spin(const F& future) {
    QElapsedTimer timer;
    timer.start();
    while (!future.isFinished() && timer.elapsed() < 15000) {
        QTest::qWait(10);
    }
}

// Правка в обход окна: /PageMode каталога; heavy — как правка страниц (полная запись).
void edit(DocumentView& view, bool heavy) {
    QFuture<void> f = view.session().edit(QStringLiteral("Test edit"), [heavy](ot::TaskContext& tc) {
        ot::Document& doc = tc.document();
        fz_context* ctx = doc.context().get();
        pdf_document* pdf = doc.pdf();
        ot::mu::call(ctx, [&] {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_dict_put_name(ctx, root, PDF_NAME(PageMode), "UseThumbs");
        });
        if (heavy) {
            tc.markHeavyEdit();
        }
    });
    spin(f);
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

class SaveFlowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-save_flow"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
    }

    void passwordDialogGivesThreeAttempts() {
        std::printf("OT_FEATURE:security-09\n");
        int checks = 0;
        PasswordDialog d(QStringLiteral("секрет.pdf"), [&checks](const QString&) {
            ++checks;
            return false;
        });
        QVERIFY(!d.okButton()->isEnabled());  // пусто — нечего проверять
        for (int i = 1; i <= 3; ++i) {
            d.field()->setText(QStringLiteral("неверный"));
            d.submit();
            QCOMPARE(d.attemptsUsed(), i);
            if (i < 3) {
                QVERIFY(d.errorLabel()->text().contains(QString::number(3 - i)));
                QVERIFY(d.field()->text().isEmpty());
            }
        }
        QVERIFY(d.exhausted());
        QCOMPARE(d.result(), static_cast<int>(QDialog::Rejected));
        d.submit();  // после трёх — не проверяет
        QCOMPARE(checks, 3);

        PasswordDialog ok(QStringLiteral("a.pdf"),
                          [](const QString& p) { return p == QStringLiteral("user"); });
        ok.field()->setText(QStringLiteral("x"));
        ok.submit();
        ok.field()->setText(QStringLiteral("user"));
        ok.submit();
        QCOMPARE(ok.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(ok.attemptsUsed(), 2);
        QCOMPARE(ok.password(), QStringLiteral("user"));
        QVERIFY(!ok.exhausted());
    }

    void openEncryptedAsksAndRemembersForSession() {
        MainWindow w;
        showActive(w);
        int prompts = 0;
        w.setPasswordPrompt([&prompts](PasswordDialog& d) {
            ++prompts;
            d.field()->setText(QStringLiteral("wrong"));
            d.submit();
            d.field()->setText(QStringLiteral("user"));
            d.rememberBox()->setChecked(true);
            d.submit();
            return d.result() == QDialog::Accepted;
        });
        const QString file = source("fixtures/enc/enc-aes-256-r6.pdf");
        QVERIFY(w.openFile(file));
        QCOMPARE(prompts, 1);
        QVERIFY(w.hasRememberedPassword(file));
        QVERIFY(w.closeDocument(0));
        QVERIFY(w.openFile(file));  // пароль из памяти — без диалога
        QCOMPARE(prompts, 1);
        QVERIFY(w.closeDocument(0));

        MainWindow other;  // память — только у окна, на диск не пишется
        QVERIFY(!other.hasRememberedPassword(file));
        other.setPasswordPrompt([](PasswordDialog& d) {
            for (int i = 0; i < 3; ++i) {
                d.field()->setText(QStringLiteral("nope"));
                d.submit();
            }
            return d.result() == QDialog::Accepted;
        });
        QVERIFY(!other.openFile(file));
        QVERIFY(other.errorString().contains(QStringLiteral("три")));
        QCOMPARE(other.documentCount(), 0);
    }

    void pdfaIsReadOnlyUntilEditingEnabled() {
        std::printf("OT_FEATURE:pro-45\n");
        const QString file = source("fixtures/pick/bfo/pdfa2-6-8-bfo-t01-pass.pdf");
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(file));
            DocumentView* view = w.currentDocument();
            QVERIFY(view->session().summary().pdfa);
            QVERIFY(view->isReadOnly());
            QVERIFY(!view->canEdit());
            ot::ui::MessageBar* bar = view->messageStack().bar(MessageKind::PdfA);
            QVERIFY(bar != nullptr);
            QCOMPARE(bar->objectName(), QStringLiteral("pdfaBar"));
            QVERIFY(bar->text().contains(QStringLiteral("PDF/A")));
            QTRY_VERIFY(w.status().findChild<ot::ui::Chip*>(QStringLiteral("readOnlyChip"))->isVisible());
            auto* enable = bar->findChild<QPushButton*>();
            QVERIFY(enable != nullptr);
            enable->click();
            QVERIFY(!view->isReadOnly());
            QVERIFY(view->canEdit());
            QVERIFY(view->messageStack().bar(MessageKind::PdfA) == nullptr);
            QTRY_VERIFY(!w.status().findChild<ot::ui::Chip*>(QStringLiteral("readOnlyChip"))->isVisible());
        }
        // Установка «Открывать PDF/A только для чтения» выключена — сразу правится.
        ot::Settings().set(ot::settings::kPdfaReadOnly, false);
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(file));
        QVERIFY(!w.currentDocument()->isReadOnly());
        QVERIFY(w.currentDocument()->messageStack().bar(MessageKind::PdfA) == nullptr);
        ot::Settings().set(ot::settings::kPdfaReadOnly, true);
    }

    void repairedFileShowsFullSaveBar() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("data/mini/truncated.pdf")));
        DocumentView* view = w.currentDocument();
        QVERIFY(view->session().summary().repaired);
        ot::ui::MessageBar* bar = view->messageStack().bar(MessageKind::Repaired);
        QVERIFY(bar != nullptr);
        QVERIFY(bar->text().contains(QStringLiteral("полным")));
    }

    void signedDocumentWarnsAndSavesCopy() {
        const QString file = copy("fixtures/sig/signed-bb.pdf", QStringLiteral("подписан.pdf"));
        const QByteArray original = readAll(file);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(file));
        DocumentView* view = w.currentDocument();
        edit(*view, true);  // тяжёлая правка — полная запись сломала бы подпись
        QTRY_VERIFY(view->isModified());
        int warnings = 0;
        w.saveFlow().setSignaturePrompt([&warnings](DocumentView&) {
            ++warnings;
            return SaveFlow::SignatureAnswer::SaveCopy;
        });
        const QString target = QDir(tmp_.path()).filePath(QStringLiteral("копия.pdf"));
        w.saveFlow().setSaveAsPrompt([&target](ot::ui::SaveAsDialog& d) {
            d.setPath(target);
            return true;
        });
        QVERIFY(w.saveFlow().save(*view));
        QCOMPARE(warnings, 1);
        QVERIFY(QFile::exists(target));
        QCOMPARE(readAll(file), original);  // подписанный файл не тронут
        QCOMPARE(QDir::cleanPath(view->path()),
                 QDir::cleanPath(file));  // документ — на своём файле с правками
        QVERIFY(view->isModified());
        // «Отмена» в предупреждении — ничего не пишется.
        w.saveFlow().setSignaturePrompt([](DocumentView&) { return SaveFlow::SignatureAnswer::Cancel; });
        QVERIFY(!w.saveFlow().save(*view));
        QVERIFY(w.saveFlow().lastError().contains(QStringLiteral("подписи")));
    }

    void saveAsWithOptionsMovesDocument() {
        const QString file = copy("fixtures/demo/report.pdf", QStringLiteral("отчёт.pdf"));
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(file));
        DocumentView* view = w.currentDocument();
        edit(*view, false);
        const QString target = QDir(tmp_.path()).filePath(QStringLiteral("отчёт-2.pdf"));
        bool signedNote = true;
        w.saveFlow().setSaveAsPrompt([&target, &signedNote](ot::ui::SaveAsDialog& d) {
            signedNote = d.signedNote()->isVisibleTo(&d);
            d.setPath(target);
            d.fullButton()->setChecked(true);
            d.objectStreamsBox()->setChecked(false);
            return true;
        });
        QTest::keyClick(&w, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(QDir::cleanPath(view->path()), QDir::cleanPath(target));
        QVERIFY(!signedNote);  // не подписан — подсказки о подписях нет
        QVERIFY(!readAll(target).contains("/ObjStm"));
        QTRY_VERIFY(!view->isModified());
        QCOMPARE(w.tabs().tabText(w.tabs().indexOf(view->id())), QStringLiteral("отчёт-2.pdf"));
        // Ctrl+S лёгкой правки — инкремент в тот же файл.
        edit(*view, false);
        QTRY_VERIFY(view->isModified());
        const qint64 before = QFileInfo(target).size();
        QTest::keyClick(&w, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!view->isModified());
        QVERIFY(QFileInfo(target).size() > before);
        QVERIFY(readAll(target).startsWith(readAll(target).left(before)));
    }

    void autosaveRecoveryAfterCrashAndDiscard() {
        std::printf("OT_FEATURE:view-27\n");
        const QString file = copy("data/mini/blank-a4.pdf", QStringLiteral("автосохранение.pdf"));
        {
            // «Падение»: окно уходит без closeEvent — запись автосохранения остаётся.
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(file));
            DocumentView* view = w.currentDocument();
            edit(*view, false);
            QFuture<ot::AutosaveStatus> saved = w.autosave().saveNow(&view->session());
            spin(saved);
            QCOMPARE(saved.result(), ot::AutosaveStatus::Saved);
            QCOMPARE(w.autosave().recoverable().size(), std::size_t{1});
        }
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        ot::ui::RecoverDialog* d = w.offerRecovery();
        QVERIFY(d != nullptr);
        QVERIFY(d->isVisible());
        QCOMPARE(d->list()->count(), 1);
        QVERIFY(d->list()->item(0)->text().contains(QStringLiteral("автосохранение.pdf")));
        d->recoverButton()->click();
        QCOMPARE(w.documentCount(), 1);
        DocumentView* view = w.currentDocument();
        QCOMPARE(QDir::cleanPath(view->path()), QDir::cleanPath(file));
        QTRY_VERIFY(view->session().undoState().canUndo);  // правка на месте, файл не сохранён
        QTRY_VERIFY(view->isModified());
        // «Не сохранять» при закрытии — запись удалена, при следующем запуске не предлагается.
        QVERIFY(w.closeDocument(0));
        QVERIFY(w.autosave().recoverable().empty());
        QVERIFY(w.offerRecovery() == nullptr);
    }

    void recoveryDiscardAndLater() {
        const QString file = copy("data/mini/blank-a4.pdf", QStringLiteral("позже.pdf"));
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(file));
            edit(*w.currentDocument(), false);
            spin(w.autosave().saveNow(&w.currentDocument()->session()));
        }
        {
            MainWindow w;
            ot::ui::RecoverDialog* d = w.offerRecovery();
            QVERIFY(d != nullptr);
            d->laterButton()->click();  // «Позже» — запись остаётся
            QCOMPARE(w.autosave().recoverable().size(), std::size_t{1});
            QCOMPARE(w.documentCount(), 0);
        }
        MainWindow w;
        ot::ui::RecoverDialog* d = w.offerRecovery();
        QVERIFY(d != nullptr);
        d->discardButton()->click();  // «Не восстанавливать» — удалить
        QVERIFY(w.autosave().recoverable().empty());
        QCOMPARE(w.documentCount(), 0);
    }

    void rewriteNoticeBar() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("data/mini/blank-a4.pdf")));
        ot::ui::MessageBar* bar =
            SaveFlow::rewriteNotice(*w.currentDocument(), QStringLiteral("Объединить файлы"));
        QCOMPARE(bar->objectName(), QStringLiteral("rewriteNoticeBar"));
        QVERIFY(bar->text().contains(QStringLiteral("Отменить эту операцию нельзя")));
        QVERIFY(w.currentDocument()->messages().contains(bar));
    }

private:
    QString copy(const char* rel, const QString& name) {
        const QString target = QDir(tmp_.path()).filePath(name);
        QFile::remove(target);
        if (!QFile::copy(source(rel), target)) {
            return {};
        }
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        return target;
    }

    QTemporaryDir tmp_;
};

OT_UI_TEST(SaveFlowTest, "save_flow");

#include "save_flow_test.moc"
