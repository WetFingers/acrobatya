// ui.signature_capture — «Подпись»/«Инициалы» Fill & Sign (план 5.3, п. 1; эталон 3.5, 6.3; sign-01,
// sign-02): «Нарисовать» — три штриха мышью (QTest::mouseMove) дают PNG с альфой и ≥ 3 связными штрихами;
// «Набрать» — имя рукописным шрифтом даёт непустую картинку с текстом, инициалы — первые буквы имени;
// «Картинка» — белый фон уходит в прозрачность, «Сохранить исходный цвет» оставляет цвет скана; «Сохранить
// подпись» пишет <данные>/signatures/{signature,initials}.png, и новый экземпляр службы читает их
// («перезапуск»); без чернил «Применить» серая.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QToolButton>
#include <QtTest/QtTest>

#include "core/util/features.h"
#include "services/saved_signatures.h"
#include "services/signature_image.h"
#include "support/ui_test.h"
#include "ui/theme/tokens.h"
#include "ui/tools/fillsign/signature_capture_dialog.h"

using ot::fillsign::SavedSignatures;
using ot::fillsign::SignatureKind;
using ot::ui::SignatureCaptureDialog;
using ot::ui::SignaturePad;

namespace {

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-signature-capture"); }

// Три отдельных штриха мышью: нажать, провести, отпустить.
void drawThreeStrokes(SignaturePad* pad) {
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

}  // namespace

class SignatureCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void drawnSignatureHasStrokesAndAlpha() {
        OT_FEATURE("sign-01");
        OT_FEATURE("sign-02");
        SignatureCaptureDialog dialog(SignatureKind::Signature);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.setMode(SignatureCaptureDialog::Mode::Draw);
        QTRY_VERIFY(dialog.pad()->isVisible() && dialog.pad()->width() > 200);
        QVERIFY(!dialog.applyButton()->isEnabled());  // ещё нечего ставить
        drawThreeStrokes(dialog.pad());
        QCOMPARE(static_cast<int>(dialog.pad()->strokes().size()), 3);
        QVERIFY(dialog.applyButton()->isEnabled());
        const QImage image = dialog.result();
        QVERIFY(!image.isNull());
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);  // угол — прозрачный
        QVERIFY2(ot::fillsign::inkComponents(image) >= 3,
                 qPrintable(QString::number(ot::fillsign::inkComponents(image))));

        // Синие чернила — у непрозрачных пикселей синий цвет.
        QTest::mouseClick(dialog.inkButton(1), Qt::LeftButton);
        QCOMPARE(dialog.inkColor(), ot::ui::kSignatureInkColors[1].color());
        const QImage blue = dialog.result();
        bool found = false;
        for (int y = 0; y < blue.height() && !found; ++y) {
            for (int x = 0; x < blue.width() && !found; ++x) {
                const QRgb p = blue.pixel(x, y);
                if (qAlpha(p) == 255) {
                    QCOMPARE(QColor::fromRgb(p).rgb() & 0xffffff,
                             ot::ui::kSignatureInkColors[1].color().rgb() & 0xffffff);
                    found = true;
                }
            }
        }
        QVERIFY(found);

        QSignalSpy captured(&dialog, &SignatureCaptureDialog::captured);
        QVERIFY(dialog.saveBox()->isChecked());
        QTest::mouseClick(dialog.applyButton(), Qt::LeftButton);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.at(0).at(0).value<SignatureKind>(), SignatureKind::Signature);
        // Сохранена и читается новым экземпляром («перезапуск»).
        const SavedSignatures saved;
        QCOMPARE(saved.root(), QDir(dataDir()).filePath(QStringLiteral("signatures")));
        QVERIFY(QFile::exists(QDir(dataDir()).filePath(QStringLiteral("signatures/signature.png"))));
        const QImage loaded = SavedSignatures().load(SignatureKind::Signature);
        QVERIFY(loaded.hasAlphaChannel());
        QVERIFY(ot::fillsign::inkComponents(loaded) >= 3);

        // «Очистить» — снова пусто.
        dialog.setMode(SignatureCaptureDialog::Mode::Draw);
        dialog.clearButton()->click();
        QVERIFY(dialog.pad()->isEmpty());
        QVERIFY(!dialog.applyButton()->isEnabled());
    }

    void typedSignatureRendersText() {
        SignatureCaptureDialog dialog(SignatureKind::Signature);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QCOMPARE(dialog.mode(), SignatureCaptureDialog::Mode::Type);
        QCOMPARE(dialog.fontBox()->count(), 2);
        QCOMPARE(dialog.fontBox()->itemText(0), QStringLiteral("Segoe Script"));
        QCOMPARE(dialog.fontBox()->itemText(1), QStringLiteral("Segoe Print"));
        dialog.nameEdit()->setText(QStringLiteral("Иван Петров"));
        const QImage script = dialog.result();
        QVERIFY(!script.isNull());
        QVERIFY2(script.width() > 100 && script.height() > 30,
                 qPrintable(QStringLiteral("%1x%2").arg(script.width()).arg(script.height())));
        QVERIFY(ot::fillsign::inkComponents(script) >= 2);  // два слова — хотя бы две области
        dialog.fontBox()->setCurrentIndex(1);
        const QImage print = dialog.result();
        QVERIFY(!print.isNull());
        QVERIFY(print.size() != script.size());  // другой шрифт — другая картинка
        dialog.nameEdit()->clear();
        QVERIFY(dialog.result().isNull());
        QVERIFY(!dialog.applyButton()->isEnabled());
    }

    void initialsAreSavedSeparately() {
        QCOMPARE(ot::fillsign::initialsOf(QStringLiteral("Иван Петров")), QStringLiteral("И.П."));
        QCOMPARE(ot::fillsign::initialsOf(QStringLiteral("  anna  maria  smith jones ")),
                 QStringLiteral("A.M.S."));
        SignatureCaptureDialog dialog(SignatureKind::Initials);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Инициалы"));
        dialog.nameEdit()->setText(QStringLiteral("И.П."));
        QVERIFY(dialog.applyButton()->isEnabled());
        dialog.accept();
        QVERIFY(SavedSignatures().has(SignatureKind::Initials));
        QVERIFY(QFile::exists(QDir(dataDir()).filePath(QStringLiteral("signatures/initials.png"))));
    }

    void imageLosesWhiteBackground() {
        // Скан: белая бумага, серая клетка светлее порога, синий штрих.
        QImage scan(300, 120, QImage::Format_RGB32);
        scan.fill(Qt::white);
        {
            QPainter p(&scan);
            p.setPen(QColor(230, 230, 230));
            for (int x = 0; x < 300; x += 15) {
                p.drawLine(x, 0, x, 119);
            }
            p.setPen(QPen(QColor(20, 40, 160), 6));
            p.drawLine(40, 60, 260, 70);
        }
        const QString path = QDir(dataDir()).filePath(QStringLiteral("scan.png"));
        QVERIFY(scan.save(path, "PNG"));
        SignatureCaptureDialog dialog(SignatureKind::Signature);
        dialog.setFileChooser([path](QWidget*) { return path; });
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.setMode(SignatureCaptureDialog::Mode::Image);
        QVERIFY(!dialog.applyButton()->isEnabled());
        QTest::mouseClick(dialog.browseButton(), Qt::LeftButton);
        QCOMPARE(dialog.mode(), SignatureCaptureDialog::Mode::Image);
        QVERIFY(dialog.applyButton()->isEnabled());
        const QImage inked = dialog.result();
        QVERIFY(inked.hasAlphaChannel());
        QCOMPARE(ot::fillsign::inkComponents(inked), 1);  // клетка ушла, штрих один
        QCOMPARE(qAlpha(inked.pixel(0, 0)), 0);
        const QRgb mid = inked.pixel(inked.width() / 2, inked.height() / 2);
        QCOMPARE(qAlpha(mid), 255);
        QCOMPARE(QColor::fromRgb(mid).rgb() & 0xffffff, 0u);  // чернила — чёрные по умолчанию
        dialog.keepColorBox()->setChecked(true);
        const QRgb kept = dialog.result().pixel(inked.width() / 2, inked.height() / 2);
        QVERIFY(qBlue(kept) > 120 && qRed(kept) < 60);
        // Порог выше клетки — клетка остаётся (сетка связывает штрих: одна область, но больше).
        dialog.thresholdSlider()->setValue(240);
        QVERIFY(dialog.result().width() > inked.width());
        QVERIFY(!dialog.loadImageFile(QDir(dataDir()).filePath(QStringLiteral("нет.png"))));
    }
};

OT_UI_TEST(SignatureCaptureTest, "signature_capture");
#include "signature_capture_test.moc"
