// ui.fillsign_place — подпись и инициалы Fill & Sign, плоская копия, подписанный документ и поля формы
// (план 5.3, п. 3; эталон 3.5, сценарий 3; sign-01, forms-07): нарисованная подпись (3 штриха) — /Stamp
// /FillSignSignature с PNG-альфой и ≥ 3 связными штрихами, стоит на линии «Подпись»; картинка с белым фоном —
// /Stamp инициалов с прозрачным фоном, протягиванием — в рамке; сохранённая ставится из меню; цвет выбранной
// подписи меняется одним шагом, рамка растягивается, Del удаляет; «Плоская копия» — новый файл без аннотаций
// проходит qpdf --check, документ остаётся с правками; подписанный документ — предупреждение, запись
// инкрементом, подпись цела; документ с полями AcroForm — подсказка «Заполнить форму», текст ставится поверх.
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

#include "core/annot/annots.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "services/saved_signatures.h"
#include "services/signature_image.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/dialogs/save_as_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/save_flow.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/tools/fillsign/fillsign_tool.h"
#include "ui/tools/fillsign/signature_capture_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::fillsign::SignatureKind;
using ot::ui::DocumentView;
using ot::ui::FillSignAction;
using ot::ui::FillSignMode;
using ot::ui::FillSignTool;
using ot::ui::InlineTextEditor;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::SignatureCaptureDialog;
using ot::ui::ToolRegistry;

namespace {

constexpr double kPageHeight = 841.8898;
double top(double pdfY) { return kPageHeight - pdfY; }
const double kSignatureBase = top(378);  // подчёркивание «Подпись», x 140–330

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-fillsign-place"); }

FillSignTool& fillSignTool() {
    return *static_cast<FillSignTool*>(ToolRegistry::instance().find(QStringLiteral("fillsign")));
}

DocumentView* openFile(MainWindow& w, const QString& path) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

// Копия анкеты в каталоге данных набора (её пишут).
QString anketa(const QString& name) {
    return ot::test::copyFixture(QStringLiteral("forms/flat/anketa.pdf"), dataDir(), name);
}

QPoint at(const PageView& v, double x, double y) { return v.pageTransform(0).map(QPointF(x, y)).toPoint(); }

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), 0).result()) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

int countOf(DocumentView& doc, AnnotType type) { return static_cast<int>(ofType(doc, type).size()); }

AnnotProps propsOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::annot::props(doc.session(), ref).result().value_or(AnnotProps{});
}

FillSignMode& startTool(DocumentView& doc) {
    ToolRegistry::instance().activate(QStringLiteral("fillsign"), &doc);
    FillSignMode& mode = FillSignTool::modeOf(doc.view());
    mode.geometry(0);
    (void)QTest::qWaitFor([&] { return mode.geometry(0) != nullptr; }, 10000);
    return mode;
}

void drawThreeStrokes(ot::ui::SignaturePad* pad) {
    const int w = pad->width();
    const int h = pad->height();
    for (int s = 0; s < 3; ++s) {
        const int x0 = w / 10 + s * w * 3 / 10;
        QTest::mousePress(pad, Qt::LeftButton, Qt::NoModifier, QPoint(x0, h / 3));
        for (int i = 1; i <= 8; ++i) {
            QTest::mouseMove(pad, QPoint(x0 + i * w / 50, h / 3 + (i % 2 == 0 ? i * 3 : -i * 2)));
        }
        QTest::mouseRelease(pad, Qt::LeftButton, Qt::NoModifier, QPoint(x0 + 8 * w / 50, h / 3 + 24));
    }
}

QImage stampImageOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::fillsign::stampImage(doc.session(), ref).result();
}

}  // namespace

class FillSignPlaceTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void drawnSignatureIsStampWithAlpha() {
        OT_FEATURE("sign-01");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openFile(w, anketa(QStringLiteral("drawn.pdf")));
        QVERIFY(doc != nullptr);
        FillSignMode& mode = startTool(*doc);
        PageView& v = doc->view();

        SignatureCaptureDialog* dialog = fillSignTool().addSignature(SignatureKind::Signature);
        QVERIFY(dialog != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        dialog->setMode(SignatureCaptureDialog::Mode::Draw);
        QTRY_VERIFY(dialog->pad()->width() > 200);
        drawThreeStrokes(dialog->pad());
        QTest::mouseClick(dialog->applyButton(), Qt::LeftButton);
        QCOMPARE(mode.action(), FillSignAction::Signature);
        QVERIFY(ot::fillsign::SavedSignatures().has(SignatureKind::Signature));  // «Сохранить подпись»

        // Щелчок над линией «Подпись» — штамп низом на линии, с точки щелчка.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 160, kSignatureBase - 8));
        QTRY_COMPARE(countOf(*doc, AnnotType::Stamp), 1);
        const AnnotInfo stamp = ofType(*doc, AnnotType::Stamp).front();
        const AnnotProps p = propsOf(*doc, stamp.ref);
        QCOMPARE(QString::fromStdString(p.icon), QStringLiteral("FillSignSignature"));
        QCOMPARE(QString::fromStdString(p.subject), QStringLiteral("Signature"));
        QVERIFY2(std::abs(p.rect.x0 - 160) < 1.5, qPrintable(QString::number(p.rect.x0)));
        QVERIFY2(p.rect.y1 > kSignatureBase && p.rect.y1 < kSignatureBase + 8,
                 qPrintable(QString::number(p.rect.y1)));
        QVERIFY(p.rect.height() > 5 && p.rect.height() <= 48);
        QVERIFY(p.rect.x1 <= 330 + 1);  // не дальше конца линии
        const QImage image = stampImageOf(*doc, stamp.ref);
        QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        QVERIFY2(ot::fillsign::inkComponents(image) >= 3,
                 qPrintable(QString::number(ot::fillsign::inkComponents(image))));
        // Поставленная выбрана, действие — снова «Текст»; в панели «Подписи» её нет (не криптографическая).
        QTRY_VERIFY(v.tools().annotSelect().isSelected(stamp.ref));
        QCOMPARE(mode.action(), FillSignAction::Text);
        QVERIFY(!doc->session().summary().isSigned);

        // Цвет выбранной — одним шагом: чернила красные, альфа прежняя.
        const QColor red(200, 0, 0);
        fillSignTool().colorSwatch()->setColor(red);
        emit fillSignTool().colorSwatch() -> colorChanged(red);
        QTRY_VERIFY([&] {
            const QImage img = stampImageOf(*doc, stamp.ref);
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (qAlpha(img.pixel(x, y)) == 255) {
                        return qRed(img.pixel(x, y)) > 150 && qBlue(img.pixel(x, y)) < 60;
                    }
                }
            }
            return false;
        }());
        QCOMPARE(ot::fillsign::inkComponents(stampImageOf(*doc, stamp.ref)),
                 ot::fillsign::inkComponents(image));
        QVERIFY(doc->session().undoState().undoName.contains(QLatin1String("Set signature color")));

        // Растянуть рамку, затем Del.
        const ot::RectF bigger{p.rect.x0, p.rect.y0 - 10, p.rect.x1 + 20, p.rect.y1};
        ot::annot::setRect(doc->session(), stamp.ref, bigger).waitForFinished();
        QTRY_VERIFY(std::abs(ofType(*doc, AnnotType::Stamp).front().rect.x1 - bigger.x1) < 1.5);
        v.tools().annotSelect().setSelection(ofType(*doc, AnnotType::Stamp));
        v.setFocus();
        QTest::keyClick(v.viewport(), Qt::Key_Delete);
        QTRY_COMPARE(countOf(*doc, AnnotType::Stamp), 0);
    }

    void imageInitialsDraggedIntoRect() {
        // Скан инициалов: белая бумага, синий штрих.
        QImage scan(240, 120, QImage::Format_RGB32);
        scan.fill(Qt::white);
        {
            QPainter painter(&scan);
            painter.setPen(QPen(QColor(20, 40, 160), 8));
            painter.drawLine(30, 90, 120, 20);
            painter.drawLine(120, 20, 210, 90);
        }
        const QString png = QDir(dataDir()).filePath(QStringLiteral("initials-scan.png"));
        QVERIFY(scan.save(png, "PNG"));

        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openFile(w, anketa(QStringLiteral("initials.pdf")));
        QVERIFY(doc != nullptr);
        FillSignMode& mode = startTool(*doc);
        PageView& v = doc->view();
        SignatureCaptureDialog* dialog = fillSignTool().addSignature(SignatureKind::Initials);
        QVERIFY(dialog != nullptr);
        dialog->setFileChooser([png](QWidget*) { return png; });
        dialog->setMode(SignatureCaptureDialog::Mode::Image);
        QTest::mouseClick(dialog->browseButton(), Qt::LeftButton);
        dialog->saveBox()->setChecked(false);  // только для этого документа
        QTest::mouseClick(dialog->applyButton(), Qt::LeftButton);
        QCOMPARE(mode.action(), FillSignAction::Initials);
        QVERIFY(!ot::fillsign::SavedSignatures().has(SignatureKind::Initials));

        // Протягивание — вписать в рамку 60 × 60 с пропорциями картинки.
        const QPoint a = at(v, 400, 200);
        const QPoint b = at(v, 460, 260);
        QTest::mousePress(v.viewport(), Qt::LeftButton, Qt::NoModifier, a);
        QTest::mouseMove(v.viewport(), (a + b) / 2);
        QTest::mouseMove(v.viewport(), b);
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, Qt::NoModifier, b);
        QTRY_COMPARE(countOf(*doc, AnnotType::Stamp), 1);
        const AnnotInfo stamp = ofType(*doc, AnnotType::Stamp).front();
        const AnnotProps p = propsOf(*doc, stamp.ref);
        QCOMPARE(QString::fromStdString(p.icon), QStringLiteral("FillSignInitials"));
        QVERIFY(std::abs(p.rect.x0 - 400) < 1.5 && std::abs(p.rect.y0 - 200) < 1.5);
        QVERIFY2(std::abs(p.rect.width() - 60) < 1.5, qPrintable(QString::number(p.rect.width())));
        QVERIFY(p.rect.height() < 40);  // картинка шире высоты — рамка ниже
        const QImage image = stampImageOf(*doc, stamp.ref);
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);  // белый фон — прозрачный
        QCOMPARE(qAlpha(image.pixel(image.width() - 1, image.height() - 1)), 0);
        bool ink = false;
        for (int x = 0; x < image.width() && !ink; ++x) {
            ink = qAlpha(image.pixel(x, image.height() / 2)) == 255;
        }
        QVERIFY(ink);

        // Сохранённой нет — «Инициалы ▾ → поставить» открывает диалог; сохранённая подпись — сразу
        // постановка.
        fillSignTool().useSaved(SignatureKind::Signature);
        QCOMPARE(mode.action(), FillSignAction::Signature);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 450, 60));
        QTRY_COMPARE(countOf(*doc, AnnotType::Stamp), 2);
        // Удалить сохранённую — крестиком меню.
        QVERIFY(fillSignTool().removeSaved(SignatureKind::Signature));
        QVERIFY(!ot::fillsign::SavedSignatures().has(SignatureKind::Signature));
    }

    void flatCopyHasNoAnnotations() {
        OT_FEATURE("forms-07");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openFile(w, anketa(QStringLiteral("flat-source.pdf")));
        QVERIFY(doc != nullptr);
        FillSignMode& mode = startTool(*doc);
        PageView& v = doc->view();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 200, top(718) - 6));
        QTRY_VERIFY(InlineTextEditor::active(v) != nullptr);
        InlineTextEditor::active(v)->setPlainText(QStringLiteral("Иванов"));
        InlineTextEditor::active(v)->commit();
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
        mode.setAction(FillSignAction::Check);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 78, top(474)));
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 2);

        const QString target = QDir(dataDir()).filePath(QStringLiteral("flat-copy.pdf"));
        QFile::remove(target);
        bool flatChecked = false;
        w.saveFlow().setSaveAsPrompt([&](ot::ui::SaveAsDialog& d) {
            flatChecked = d.flatCopyBox()->isChecked() && !d.incrementalButton()->isEnabled();
            d.setPath(target);
            return true;
        });
        QTest::mouseClick(fillSignTool().flattenButton(), Qt::LeftButton);
        QVERIFY(flatChecked);
        QTRY_VERIFY(QFile::exists(target));
        const ot::QpdfTool qpdf;
        if (qpdf.available()) {
            const ot::ToolResult check = qpdf.check(ot::paths::fromUtf8(target.toStdString()));
            QVERIFY2(check.ok(), check.output.c_str());
        }
        // В копии аннотаций нет, текст стал частью страницы; документ — со своими правками на своём файле.
        ot::DocumentSession copy;
        QVERIFY(copy.open(target));
        QVERIFY(ot::annot::list(copy, 0).result().empty());
        QCOMPARE(countOf(*doc, AnnotType::FreeText), 2);
        QVERIFY(doc->isModified());
        QCOMPARE(QFileInfo(doc->path()).fileName(), QStringLiteral("flat-source.pdf"));
        w.saveFlow().setSaveAsPrompt({});
    }

    void signedDocumentWarnsAndSavesIncrementally() {
        const QString path = anketa(QStringLiteral("signed.pdf"));
        ot::test::signFile(path);
        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray signedBytes = in.readAll();
        in.close();

        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openFile(w, path);
        QVERIFY(doc != nullptr);
        FillSignMode& mode = startTool(*doc);
        QVERIFY(fillSignTool().signedNote()->isVisible());
        QVERIFY(fillSignTool().lockReason().isEmpty());  // поверх подписанного — можно
        PageView& v = doc->view();
        mode.placeAt(0, QPointF(200, top(718) - 6));
        QTRY_VERIFY(InlineTextEditor::active(v) != nullptr);
        InlineTextEditor::active(v)->setPlainText(QStringLiteral("Петров"));
        InlineTextEditor::active(v)->commit();
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
        bool asked = false;
        w.saveFlow().setSignaturePrompt([&asked](DocumentView&) {
            asked = true;  // запись заденет подпись — для Fill & Sign так быть не должно
            return ot::ui::SaveFlow::SignatureAnswer::Cancel;
        });
        const bool ok = w.saveFlow().save(*doc);
        w.saveFlow().setSignaturePrompt({});
        QVERIFY2(!asked, qPrintable(w.saveFlow().lastError()));
        QVERIFY2(ok, qPrintable(w.saveFlow().lastError()));
        QFile out(path);
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QByteArray saved = out.readAll();
        QVERIFY(saved.size() > signedBytes.size());
        QVERIFY(saved.startsWith(signedBytes));  // инкремент: подписанная ревизия байт в байт
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification result = ot::test::verifyFile(path, &trust);
        QCOMPARE(static_cast<int>(result.signatures.size()), 1);
        QVERIFY(result.signatures.front().intact);
        QVERIFY(result.signatures.front().verdict != ot::sign::Verdict::Invalid);
    }

    void acroFormGetsFillFormHint() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openFile(w, ot::test::copyFixture(QStringLiteral("forms/fields-all.pdf"),
                                                              dataDir(), QStringLiteral("form.pdf")));
        QVERIFY(doc != nullptr);
        FillSignMode& mode = startTool(*doc);
        QVERIFY(fillSignTool().formHint()->isVisible());
        QVERIFY(fillSignTool().fillFormButton()->isVisible());
        QTest::mouseClick(fillSignTool().fillFormButton(),
                          Qt::LeftButton);  // заглушка 6.1 — тост, не падение
        // Поверх плоских областей текст ставится.
        mode.placeAt(0, QPointF(300, 760));
        QTRY_VERIFY(InlineTextEditor::active(doc->view()) != nullptr);
        InlineTextEditor::active(doc->view())->setPlainText(QStringLiteral("Примечание"));
        InlineTextEditor::active(doc->view())->commit();
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
    }
};

OT_UI_TEST(FillSignPlaceTest, "fillsign_place");
#include "fillsign_place_test.moc"
