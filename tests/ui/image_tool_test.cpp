// ui.image_tool — картинки режима «Редактировать» (план 3.4, п. 3; эталон 3.9 «Инструмент»; edit-06, edit-07,
// create-32): images.pdf — две картинки; щелчок выбирает, перетаскивание двигает (шаг «Move image», матрица
// модели сдвинута на перетаскивание / масштаб, растр страницы другой), маркер — масштаб; меню — поворот;
// обрезка рамкой и Enter — обтравка; «Заменить…» (файл — setFileChooser) — новый ресурс, копия документа
// проходит qpdf --check; «Редактировать в…» — FakeShellLauncher получил временный файл, его перезапись
// заменяет картинку; Ctrl+C / Ctrl+V — третья картинка; Del — удаление; экспорт — JPEG как есть; ADJUST
// OBJECTS серые без выбора.
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QtTest/QtTest>

#include <cmath>

#include "core/optimize/qpdf_tool.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/shell_launcher.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/tools/edit/edit_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::EditMode;
using ot::ui::EditTool;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

EditTool& editTool() {
    return *static_cast<EditTool*>(ToolRegistry::instance().find(QStringLiteral("edit")));
}

DocumentView* openImages(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture("edit/images.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("edit"), doc));
        QTRY_VERIFY_WITH_TIMEOUT(EditTool::modeOf(doc->view()).hasContent(0), 10000);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint pageToView(const PageView& v, double x, double y) {
    return v.pageTransform(0).map(QPointF(x, y)).toPoint();
}

// Картинки страницы после правки: ждать новые блоки и картинки.
void waitStep(DocumentView& doc, EditMode& mode, const QString& step) {
    QTRY_COMPARE_WITH_TIMEOUT(doc.session().undoState().undoName, step, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(mode.hasContent(0), 10000);
}

int imageCount(EditMode& mode) {
    return mode.hasContent(0) ? static_cast<int>(mode.content(0)->images.size()) : -1;
}

// Картинка index страницы 1 (данные ещё не пришли — пустая: рамка 0, имя пусто).
ot::edit::PageImage imageOf(EditMode& mode, int index) {
    const ot::edit::PageImage* p = mode.image({0, index});
    return p != nullptr ? *p : ot::edit::PageImage{};
}

bool clipboardHas(const QString& format) {
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    return data != nullptr && data->hasFormat(format);
}

bool near(float a, float b, float eps = 0.6f) { return std::fabs(a - b) <= eps; }

}  // namespace

class ImageToolTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-image_tool"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() {
        ToolRegistry::instance().deactivate();
        EditMode::setFileChooser({});
    }

    void clickSelectsDragMovesMatrix() {
        std::printf("OT_FEATURE:edit-06\n");
        MainWindow w;
        DocumentView* doc = openImages(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        QCOMPARE(imageCount(mode), 2);
        QVERIFY(editTool().objectButton(QStringLiteral("imageRotateCw")) != nullptr);
        QVERIFY(!editTool().objectButton(QStringLiteral("imageRotateCw"))->isEnabled());
        const ot::edit::PageImage before = imageOf(mode, 0);
        const QPoint center = pageToView(v, 168, 240);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, center);
        QCOMPARE(mode.selectedImage(), (EditMode::ImageRef{0, 0}));
        QVERIFY(editTool().objectButton(QStringLiteral("imageRotateCw"))->isEnabled());
        QCOMPARE(mode.gripAt(center), EditMode::Grip::Inside);
        const QImage beforeShot = v.grab().toImage();

        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, center);
        QTest::mouseMove(v.viewport(), center + QPoint(20, 10));
        QTest::mouseMove(v.viewport(), center + QPoint(40, 20));
        QVERIFY(mode.imageFrame().has_value());
        QVERIFY(mode.imageFrame()->left() > v.pageTransform(0).map(QPointF(72, 0)).x() + 30);
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, center + QPoint(40, 20));
        waitStep(*doc, mode, QStringLiteral("Move image"));
        const auto dx = static_cast<float>(40 / v.zoom());
        const auto dy = static_cast<float>(20 / v.zoom());
        QTRY_VERIFY_WITH_TIMEOUT(near(imageOf(mode, 0).bbox.x0, before.bbox.x0 + dx), 10000);
        const ot::edit::PageImage after = imageOf(mode, 0);
        QVERIFY(near(after.bbox.y0, before.bbox.y0 + dy));
        // Матрица модели: сдвиг пространства пользователя (y вверх) — dx и −dy.
        QVERIFY(near(after.matrix.e, before.matrix.e + dx));
        QVERIFY(near(after.matrix.f, before.matrix.f - dy));
        QVERIFY(near(after.matrix.a, before.matrix.a, 0.01f));
        QTRY_VERIFY_WITH_TIMEOUT(v.grab().toImage() != beforeShot, 10000);  // растр другой
        QCOMPARE(mode.selectedImage(), (EditMode::ImageRef{0, 0}));

        // Маркер справа внизу — масштаб.
        QTRY_VERIFY_WITH_TIMEOUT(mode.imageFrame().has_value(), 10000);
        const QPointF corner = mode.imageFrame()->bottomRight();
        QCOMPARE(mode.gripAt(corner), EditMode::Grip::BottomRight);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, corner.toPoint());
        QTest::mouseMove(v.viewport(), (corner + QPointF(30, 30)).toPoint());
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, (corner + QPointF(30, 30)).toPoint());
        waitStep(*doc, mode, QStringLiteral("Resize image"));
        QTRY_VERIFY_WITH_TIMEOUT(imageOf(mode, 0).bbox.width() > after.bbox.width() + 15, 10000);
        // Undo — на прежнем месте.
        QVERIFY(doc->session().undo().result());
        QVERIFY(doc->session().undo().result());
        QTRY_VERIFY_WITH_TIMEOUT(mode.hasContent(0) && near(imageOf(mode, 0).bbox.x0, before.bbox.x0), 10000);
    }

    void menuRotateCropAndDelete() {
        std::printf("OT_FEATURE:edit-07\n");
        MainWindow w;
        DocumentView* doc = openImages(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        mode.selectImage({0, 0});
        std::unique_ptr<QMenu> menu(mode.imageMenu(nullptr));
        // Рубеж Р2: и произвольный угол (план 3.4: поворот 90°/произвольный) — диалог угла.
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("imageRotateAngle")) != nullptr);
        menu->findChild<QAction*>(QStringLiteral("imageRotateCw"))->trigger();
        waitStep(*doc, mode, QStringLiteral("Rotate image"));
        QTRY_VERIFY_WITH_TIMEOUT(near(imageOf(mode, 0).bbox.width(), 144), 10000);
        QVERIFY(near(imageOf(mode, 0).bbox.height(), 192));

        // Обрезка: кнопка ADJUST OBJECTS, рамка, Enter.
        QTest::mouseClick(editTool().objectButton(QStringLiteral("imageCrop")), Qt::LeftButton);
        QVERIFY(mode.cropping());
        const ot::RectF full = imageOf(mode, 0).bbox;
        mode.setCropRect(ot::RectF{full.x0 + 10, full.y0 + 20, full.x1 - 10, full.y1 - 40});
        v.setFocus();
        QTest::keyClick(&v, Qt::Key_Return);
        QVERIFY(!mode.cropping());
        waitStep(*doc, mode, QStringLiteral("Crop image"));
        QTRY_VERIFY_WITH_TIMEOUT(imageOf(mode, 0).clipped(), 10000);
        QVERIFY(near(imageOf(mode, 0).bbox.x0, full.x0 + 10));
        QVERIFY(near(imageOf(mode, 0).bbox.y1, full.y1 - 40));

        // Del — удалить.
        mode.selectImage({0, 1});
        QTest::keyClick(&v, Qt::Key_Delete);
        waitStep(*doc, mode, QStringLiteral("Delete image"));
        QTRY_COMPARE_WITH_TIMEOUT(imageCount(mode), 1, 10000);
        QVERIFY(!mode.selectedImage().valid());
    }

    void replaceKeepsPlaceAndQpdfChecks() {
        MainWindow w;
        DocumentView* doc = openImages(w);
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        const QString logo = fixture("images/logo.png");
        EditMode::setFileChooser(
            [logo](QWidget*, bool save, const QString&) { return save ? QString() : logo; });
        mode.selectImage({0, 1});
        QTest::mouseClick(editTool().objectButton(QStringLiteral("imageReplace")), Qt::LeftButton);
        waitStep(*doc, mode, QStringLiteral("Replace image"));
        QTRY_VERIFY_WITH_TIMEOUT(imageOf(mode, 1).name == "OtIm1", 10000);
        QVERIFY(imageOf(mode, 1).width != 32);
        QVERIFY(near(imageOf(mode, 1).bbox.x0, 320));
        // Копия документа — qpdf --check = 0.
        const QString copy = QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-image_tool/replaced.pdf");
        QFile::remove(copy);
        const ot::SaveReport saved = doc->session().saveCopy(copy).result();
        QVERIFY2(saved.ok(), saved.message.c_str());
        const ot::QpdfTool qpdf;
        if (!qpdf.available()) {
            QSKIP("qpdf не найден");
        }
        const ot::ToolResult check = qpdf.check(copy.toStdWString());
        QVERIFY2(check.ok(), check.output.c_str());
    }

    void editExternallyReplacesOnSave() {
        MainWindow w;
        DocumentView* doc = openImages(w);
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        auto* shell = dynamic_cast<ot::FakeShellLauncher*>(&ot::ShellLauncher::instance());
        QVERIFY(shell != nullptr);
        shell->calls.clear();
        mode.selectImage({0, 0});
        mode.editImageExternally();
        QTRY_VERIFY_WITH_TIMEOUT(!shell->calls.isEmpty(), 10000);
        QCOMPARE(shell->calls.back().kind, QStringLiteral("openFile"));
        const QString path = shell->calls.back().target;
        QCOMPARE(path, mode.externalFile());
        QVERIFY(path.endsWith(QStringLiteral(".jpg")));  // DCT — JPEG как есть
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(original.read(2) == QByteArray("\xFF\xD8", 2));
        original.close();
        // «Внешняя программа» сохранила файл.
        QFile logo(fixture("images/logo.png"));
        QVERIFY(logo.open(QIODevice::ReadOnly));
        const QByteArray bytes = logo.readAll();
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(bytes);
        out.close();
        waitStep(*doc, mode, QStringLiteral("Replace image"));
        QTRY_VERIFY_WITH_TIMEOUT(imageOf(mode, 0).name == "OtIm1", 10000);
    }

    void copyPasteAndExport() {
        std::printf("OT_FEATURE:create-32\n");
        MainWindow w;
        DocumentView* doc = openImages(w);
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        mode.selectImage({0, 0});
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(&v, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(clipboardHas(QStringLiteral("application/x-acrobatya-image")), 10000);
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());
        mode.clearImageSelection();
        QTest::keyClick(&v, Qt::Key_V, Qt::ControlModifier);
        waitStep(*doc, mode, QStringLiteral("Add image"));
        QTRY_COMPARE_WITH_TIMEOUT(imageCount(mode), 3, 10000);
        // Вставленная — того же размера и выбрана.
        QTRY_COMPARE_WITH_TIMEOUT(mode.selectedImage(), (EditMode::ImageRef{0, 2}), 10000);
        QVERIFY(near(imageOf(mode, 2).bbox.width(), 192));

        // Экспорт — JPEG как есть (имя без расширения получает .jpg).
        const QString target = QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-image_tool/exported");
        QFile::remove(target + QStringLiteral(".jpg"));
        EditMode::setFileChooser(
            [target](QWidget*, bool save, const QString&) { return save ? target : QString(); });
        mode.selectImage({0, 0});
        mode.exportImage();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(target + QStringLiteral(".jpg")), 10000);
        QFile f(target + QStringLiteral(".jpg"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.read(2) == QByteArray("\xFF\xD8", 2));
    }
};

OT_UI_TEST(ImageToolTest, "image_tool");
#include "image_tool_test.moc"
