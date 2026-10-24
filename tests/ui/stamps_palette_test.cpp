// ui.stamps_palette — штампы и вложения (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп (K) — палитра
// категорий, из буфера, из файла; Вложить файл; 6.3 «Палитра штампов (Создать/Управлять, Identity)»;
// comment-11, comment-12, comment-13, comment-14, comment-41): каталог — свои штампы трёх категорий,
// пользовательские из PNG и страницы PDF (создать, переименовать, удалить), избранное, значения динамических
// (Identity, дата dd.MM.yyyy); палитра — категории, миниатюры, «Выбрать», «Из буфера обмена»; инструмент
// «Штамп» — щелчок и протягивание, картинка буфера один раз, K; маркер поворота «Выбора объектов» (Shift —
// 15°, undo); «Вложить файл» — значок и файл в PDF, двойной щелчок открывает его через панель «Вложения» с
// подтверждением.
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/annot/stamps.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/shell_launcher.h"
#include "services/stamps_catalog.h"
#include "support/ui_test.h"
#include "ui/dialogs/stamps_palette.h"
#include "ui/panes/attachments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::AnnotSelectTool;
using ot::ui::CommentAction;
using ot::ui::CommentMode;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::StampsPalette;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;
namespace stamps = ot::stamps;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-stamps"); }

CommentTool& commentTool() {
    return *static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
}

DocumentView* openDoc(MainWindow& w, const char* rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y, int page = 0) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type, int page = 0) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), page).result()) {
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

void drag(PageView& v, QPoint a, QPoint b, Qt::KeyboardModifiers mods = {}) {
    QTest::mousePress(v.viewport(), Qt::LeftButton, mods, a);
    QTest::mouseMove(v.viewport(), (a + b) / 2);
    QTest::mouseMove(v.viewport(), b);
    QTest::mouseRelease(v.viewport(), Qt::LeftButton, mods, b);
}

bool near(double a, double b, double eps = 1.5) { return std::abs(a - b) < eps; }

// Картинка-штамп: синий прямоугольник с белой серединой.
QImage logo(int w = 120, int h = 60) {
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.fillRect(QRect(0, 0, w, h), QColor(0x15, 0x65, 0xC0));
    p.fillRect(QRect(w / 4, h / 4, w / 2, h / 2), Qt::white);
    return image;
}

QString savePng(const QString& name, const QImage& image) {
    const QString path = dataDir() + QLatin1Char('/') + name;
    image.save(path, "PNG");
    return path;
}

}  // namespace

class StampsPaletteTest : public QObject {
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

    void cleanup() {
        ToolRegistry::instance().deactivate();
        CommentMode::setFilePrompt({});
    }

    void catalogBuiltInUserAndFavorites() {
        OT_FEATURE("comment-11");
        OT_FEATURE("comment-12");
        const stamps::StampCatalog catalog;
        QCOMPARE(catalog.userRoot(), QDir(dataDir()).filePath(QStringLiteral("stamps")));
        const std::vector<stamps::StampEntry> builtIn = catalog.builtIn();
        QCOMPARE(static_cast<int>(builtIn.size()), 13);
        QStringList categories;
        for (const stamps::StampEntry& e : builtIn) {
            if (!categories.contains(e.category)) {
                categories << e.category;
            }
            QVERIFY2(!catalog.pdf(e).isEmpty(), qPrintable(e.id));
            const ot::annot::StampSize size = catalog.size(e);
            QVERIFY2(size.width > 50 && size.height > 20, qPrintable(e.id));
            QVERIFY(!e.name.empty() && e.name.front() == '#');
            QCOMPARE(e.dynamic, !e.lines.empty());
            QVERIFY(!StampsPalette::stampTitle(e).isEmpty());
        }
        QCOMPARE(categories, (QStringList{QStringLiteral("standard"), QStringLiteral("sign-here"),
                                          QStringLiteral("dynamic")}));
        QCOMPARE(catalog.find(QStringLiteral("standard/approved"))->name, std::string("#Approved"));
        QCOMPARE(catalog.find(QStringLiteral("standard/draft"))->name, std::string("#Draft"));
        QVERIFY(catalog.find(QStringLiteral("dynamic/approved"))->dynamic);
        QVERIFY(!catalog.find(QStringLiteral("standard/nothing")).has_value());

        // Из картинки, из PNG-файла и со страницы PDF — <данные>/stamps/<категория>/<имя>.pdf.
        stamps::StampCatalog user;
        QString error;
        const auto fromImage =
            user.createFromImage(logo(), QStringLiteral("Мои"), QStringLiteral("Лого"), &error);
        QVERIFY2(fromImage.has_value(), qPrintable(error));
        QCOMPARE(fromImage->id, QStringLiteral("user/Мои/Лого"));
        QVERIFY(QFile::exists(fromImage->path));
        QVERIFY(!fromImage->builtIn);
        QVERIFY(near(user.size(*fromImage).width, 90, 1) &&
                near(user.size(*fromImage).height, 45, 1));  // 96 dpi
        const auto fromPng = user.createFromFile(savePng(QStringLiteral("seal.png"), logo(200, 200)), 0,
                                                 QStringLiteral("Мои"), QStringLiteral("Печать"), &error);
        QVERIFY2(fromPng.has_value(), qPrintable(error));
        const auto fromPdf = user.createFromFile(fixture("edit/latin.pdf"), 0, QStringLiteral("Страницы"),
                                                 QStringLiteral("Бланк/1"), &error);
        QVERIFY2(fromPdf.has_value(), qPrintable(error));
        QCOMPARE(fromPdf->caption, QStringLiteral("Бланк_1"));  // запрещённое в Windows — «_»
        QVERIFY(user.size(*fromPdf).width > 500);
        QVERIFY(!user.createFromFile(dataDir() + QStringLiteral("/нет.png"), 0, QStringLiteral("Мои"),
                                     QStringLiteral("X"), &error));
        QVERIFY(!user.createFromImage(logo(), QStringLiteral("Мои"), QString(), &error));
        QCOMPARE(user.userCategories(), (QStringList{QStringLiteral("Мои"), QStringLiteral("Страницы")}));
        QCOMPARE(static_cast<int>(user.user().size()), 3);
        QCOMPARE(stamps::safeName(QStringLiteral("a/b:c*?")), QStringLiteral("a_b_c__"));

        // Переименовать (и в другую категорию), удалить: последняя в категории уносит папку.
        const auto renamed = user.rename(fromPng->id, QStringLiteral("Круглая"), QStringLiteral("Печати"));
        QVERIFY(renamed.has_value());
        QCOMPARE(renamed->id, QStringLiteral("user/Печати/Круглая"));
        QVERIFY(!QFile::exists(fromPng->path));
        QVERIFY(user.remove(fromPdf->id));
        QVERIFY(!QDir(QDir(user.userRoot()).filePath(QStringLiteral("Страницы"))).exists());
        QCOMPARE(user.userCategories(), (QStringList{QStringLiteral("Мои"), QStringLiteral("Печати")}));

        // Избранное — state.sqlite.
        QVERIFY(user.favorites().isEmpty());
        user.setFavorite(QStringLiteral("standard/draft"), true);
        user.setFavorite(renamed->id, true);
        QVERIFY(stamps::StampCatalog().isFavorite(QStringLiteral("standard/draft")));
        QCOMPARE(static_cast<int>(stamps::StampCatalog().favorites().size()), 2);
        user.setFavorite(QStringLiteral("standard/draft"), false);
        QCOMPARE(stamps::StampCatalog().favorites(), QStringList{renamed->id});
        QVERIFY(user.remove(renamed->id));
        user.setFavorite(renamed->id, false);
        QVERIFY(user.remove(fromImage->id));
        QVERIFY(user.userCategories().isEmpty());
    }

    void dynamicValuesFromIdentity() {
        OT_FEATURE("comment-13");
        ot::Settings settings;
        settings.set(ot::settings::kCommentIdentityName, QStringLiteral("Мария Иванова"));
        settings.set(ot::settings::kCommentIdentityTitle, QStringLiteral("Бухгалтер"));
        settings.set(ot::settings::kCommentIdentityOrganization, QStringLiteral("ООО «Ромашка»"));
        const QDateTime when(QDate(2026, 3, 5), QTime(9, 7));
        const ot::annot::StampValues v = stamps::stampValues(settings, when);
        QCOMPARE(QString::fromStdString(v.name), QStringLiteral("Мария Иванова"));
        QCOMPARE(QString::fromStdString(v.title), QStringLiteral("Бухгалтер"));
        QCOMPARE(QString::fromStdString(v.org), QStringLiteral("ООО «Ромашка»"));
        QCOMPARE(QString::fromStdString(v.date), QStringLiteral("05.03.2026"));
        QCOMPARE(QString::fromStdString(v.time), QStringLiteral("09:07"));
        QCOMPARE(QString::fromStdString(ot::annot::expandStampText("{name}, {date} {time}", v)),
                 QStringLiteral("Мария Иванова, 05.03.2026 09:07"));
        settings.set(ot::settings::kCommentIdentityName, QString());
        QCOMPARE(stamps::stampValues(settings, when).name, ot::annot::loginName());  // нет имени — имя входа
        settings.set(ot::settings::kCommentIdentityTitle, QString());
        settings.set(ot::settings::kCommentIdentityOrganization, QString());
    }

    void paletteCategoriesCreateManageChoose() {
        OT_FEATURE("comment-12");
        StampsPalette palette;
        palette.show();
        QVERIFY(QTest::qWaitForWindowExposed(&palette));
        QCOMPARE(palette.categoryList()->count(),
                 4);  // Избранное, Стандартные, Подписать здесь, Динамические
        QCOMPARE(palette.currentCategory(), QStringLiteral("standard"));
        QCOMPARE(palette.stampList()->count(), 7);
        QVERIFY(!palette.stampList()->item(0)->icon().isNull());
        QCOMPARE(palette.stampList()->item(0)->text(), QStringLiteral("Утверждено"));
        palette.showCategory(QStringLiteral("dynamic"));
        QCOMPARE(palette.stampList()->count(), 3);
        palette.showCategory(QStringLiteral("sign-here"));
        QCOMPARE(palette.stampList()->count(), 3);
        QVERIFY(!palette.renameButton()->isEnabled() && !palette.deleteButton()->isEnabled());

        // Избранное: пусто — подсказка; добавить — штамп в категории.
        palette.showCategory(QStringLiteral("favorites"));
        QCOMPARE(palette.stampList()->count(), 0);
        QVERIFY(palette.noteLabel()->isVisibleTo(&palette));
        palette.showCategory(QStringLiteral("standard"));
        palette.selectStamp(QStringLiteral("standard/final"));
        QCOMPARE(palette.selectedStamp(), QStringLiteral("standard/final"));
        QVERIFY(palette.favoriteButton()->isEnabled());
        palette.toggleFavorite();
        QCOMPARE(palette.favoriteButton()->text(), QStringLiteral("Убрать из &избранного"));
        palette.showCategory(QStringLiteral("favorites"));
        QCOMPARE(palette.stampList()->count(), 1);
        QVERIFY(!palette.noteLabel()->isVisibleTo(&palette));

        // Создать из PNG: категория и имя — подмена диалога; новая категория слева, штамп выбран.
        const QString png = savePng(QStringLiteral("sign.png"), logo(160, 64));
        palette.setFilePrompt([png] { return png; });
        QStringList offered;
        palette.setNamePrompt([&](const QString& name, const QStringList& categories) {
            offered = categories;
            return std::optional(std::make_pair(QStringLiteral("Мои штампы"), name + QStringLiteral(" 1")));
        });
        palette.createStamp();
        QCOMPARE(offered, QStringList{QStringLiteral("Мои штампы")});  // категорий ещё нет — предложение
        QCOMPARE(palette.categoryList()->count(), 5);
        QCOMPARE(palette.currentCategory(), QStringLiteral("user:Мои штампы"));
        QCOMPARE(palette.selectedStamp(), QStringLiteral("user/Мои штампы/sign 1"));
        QVERIFY(palette.renameButton()->isEnabled() && palette.deleteButton()->isEnabled());

        // Переименовать и удалить (подтверждение — подмена).
        palette.setNamePrompt([](const QString&, const QStringList&) {
            return std::optional(std::make_pair(QStringLiteral("Мои штампы"), QStringLiteral("Подпись")));
        });
        palette.renameStamp();
        QCOMPARE(palette.selectedStamp(), QStringLiteral("user/Мои штампы/Подпись"));
        QCOMPARE(palette.stampList()->item(0)->text(), QStringLiteral("Подпись"));
        QStringList confirmed;
        palette.setConfirm([&](const QString& title) {
            confirmed << title;
            return true;
        });
        palette.deleteStamp();
        QCOMPARE(confirmed, QStringList{QStringLiteral("Подпись")});
        QCOMPARE(palette.categoryList()->count(), 4);
        QVERIFY(palette.catalog().user().empty());

        // «Выбрать» — сигнал с id и закрытие; «Из буфера обмена» без картинки — подсказка.
        palette.showCategory(QStringLiteral("standard"));
        palette.selectStamp(QStringLiteral("standard/confidential"));
        QSignalSpy chosen(&palette, &StampsPalette::stampChosen);
        palette.findChild<QPushButton*>(QStringLiteral("stampChoose"))->click();
        QCOMPARE(chosen.size(), 1);
        QCOMPARE(chosen.front().front().toString(), QStringLiteral("standard/confidential"));
        QCOMPARE(palette.result(), static_cast<int>(QDialog::Accepted));
        palette.show();
        QApplication::clipboard()->clear();
        QSignalSpy fromClipboard(&palette, &StampsPalette::clipboardStampChosen);
        palette.useClipboard();
        QCOMPARE(fromClipboard.size(), 0);
        QVERIFY(palette.noteLabel()->isVisibleTo(&palette));
        QApplication::clipboard()->setImage(logo());
        palette.useClipboard();
        QCOMPARE(fromClipboard.size(), 1);
        stamps::StampCatalog().setFavorite(QStringLiteral("standard/final"), false);
    }

    void stampToolClickDragAndClipboard() {
        OT_FEATURE("comment-11");
        OT_FEATURE("comment-13");
        OT_FEATURE("comment-41");
        ot::Settings().set(ot::settings::kCommentIdentityName, QStringLiteral("Мария Иванова"));
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        CommentTool& tool = commentTool();
        QVERIFY(tool.panel() != nullptr);
        QVERIFY(tool.actionButton(CommentAction::Stamp) != nullptr);
        QVERIFY(tool.actionButton(CommentAction::Attach) != nullptr);
        QVERIFY(tool.panel()->findChild<ot::ui::ToolButton*>(QStringLiteral("commentStampPalette")) !=
                nullptr);
        QCOMPARE(tool.stampLabel()->text(), QStringLiteral("Штамп: Утверждено"));
        bool stampKey = false;
        for (const ot::Command& c : tool.commands()) {
            if (c.id == QLatin1String("tool.stamp")) {
                stampKey =
                    c.shortcuts.size() == 1 && c.shortcuts.front() == QKeySequence(Qt::Key_K) && c.singleKey;
            }
        }
        QVERIFY(stampKey);

        // Палитра → динамический штамп → щелчок: штамп в своём размере центром в точке щелчка.
        StampsPalette* palette = tool.showStampPalette();
        QVERIFY(palette != nullptr);
        palette->showCategory(QStringLiteral("dynamic"));
        palette->selectStamp(QStringLiteral("dynamic/approved"));
        palette->findChild<QPushButton*>(QStringLiteral("stampChoose"))->click();
        QCOMPARE(v.tools().tool(), ViewToolId::Comment);
        CommentMode& mode = CommentTool::modeOf(v);
        QCOMPARE(mode.action(), CommentAction::Stamp);
        QCOMPARE(mode.stamp(), QStringLiteral("dynamic/approved"));
        QCOMPARE(ot::Settings().get(ot::settings::kCommentStamp), QStringLiteral("dynamic/approved"));
        QVERIFY(tool.stampLabel()->text().contains(QStringLiteral("Утверждено (имя, дата и время)")));
        const ot::annot::StampSize size =
            stamps::StampCatalog().size(*stamps::StampCatalog().find(QStringLiteral("dynamic/approved")));
        QTest::mouseMove(v.viewport(), at(v, 300, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 300));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Stamp), 1, 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add stamp"));
        QTRY_VERIFY(v.tools().tool() != ViewToolId::Comment);  // однократный
        AnnotProps p = propsOf(*doc, ofType(*doc, AnnotType::Stamp).front().ref);
        QCOMPARE(QString::fromStdString(p.icon), QStringLiteral("#DApproved"));
        QVERIFY2(near(p.rect.width(), size.width, 1) && near(p.rect.height(), size.height, 1),
                 qPrintable(QStringLiteral("%1 × %2").arg(p.rect.width()).arg(p.rect.height())));
        QVERIFY(near((p.rect.x0 + p.rect.x1) / 2, 300, 1.5) && near((p.rect.y0 + p.rect.y1) / 2, 300, 1.5));
        QVERIFY(ofType(*doc, AnnotType::Stamp).front().hasAppearance);

        // Протягивание — ширина рамки, пропорции штампа.
        tool.start(CommentAction::Stamp);
        drag(v, at(v, 100, 500), at(v, 250, 520));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Stamp), 2, 10000);
        p = propsOf(*doc, ofType(*doc, AnnotType::Stamp).back().ref);
        QVERIFY(near(p.rect.width(), 150, 2));
        QVERIFY(near(p.rect.height(), 150 * size.height / size.width, 2));

        // Картинка буфера — один раз, затем снова штамп каталога.
        QApplication::clipboard()->setImage(logo(96, 48));
        palette = tool.showStampPalette();
        palette->useClipboard();
        QVERIFY(mode.hasClipboardStamp());
        QCOMPARE(mode.action(), CommentAction::Stamp);
        QCOMPARE(tool.stampLabel()->text(), QStringLiteral("Штамп: картинка из буфера обмена"));
        QTest::mouseMove(v.viewport(), at(v, 400, 650));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 650));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::Stamp), 3, 10000);
        QVERIFY(!mode.hasClipboardStamp());
        QTRY_VERIFY(tool.stampLabel()->text().contains(QStringLiteral("Утверждено (имя, дата и время)")));
        p = propsOf(*doc, ofType(*doc, AnnotType::Stamp).back().ref);
        QCOMPARE(QString::fromStdString(p.icon), QStringLiteral("Image"));
        QVERIFY(near(p.rect.width(), 72, 1) && near(p.rect.height(), 36, 1));  // 96 × 48 px при 96 dpi
        ot::Settings().set(ot::settings::kCommentIdentityName, QString());
        ot::Settings().set(ot::settings::kCommentStamp, QStringLiteral("standard/approved"));
    }

    void rotateHandleTurnsStamp() {
        OT_FEATURE("comment-11");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        const stamps::StampCatalog catalog;
        const auto entry = catalog.find(QStringLiteral("standard/draft"));
        QVERIFY(entry.has_value());
        AnnotProps sp = ot::annot::toolDefaults(AnnotType::Stamp);
        const ot::annot::StampSize size = catalog.size(*entry);
        sp.rect = {200, 300, 200 + size.width, 300 + size.height};
        const auto ref = ot::annot::createStamp(doc->session(), 0, catalog.source(*entry), {}, sp).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        AnnotSelectTool& select = v.tools().annotSelect();
        select.setSelection(ofType(*doc, AnnotType::Stamp));
        QTRY_VERIFY_WITH_TIMEOUT(select.rotateHandle().has_value(), 10000);
        QCOMPARE(*select.stampAngle(), 0.0f);
        // Маркер над центром → вправо от центра: +90° по часовой (Shift — к шагу 15°).
        const QPointF handle = *select.rotateHandle();
        const QPointF center = select.frames().front().center();
        const QPointF right = center + QPointF(center.y() - handle.y(), 3);
        drag(v, handle.toPoint(), right.toPoint(), Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(near(ot::annot::stampRotation(doc->session(), ref).result(), 90, 0.01),
                                 10000);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Rotate stamp"));
        // Повёрнутая рамка — высота и ширина меняются местами, центр прежний.
        const AnnotProps turned = propsOf(*doc, ref);
        QVERIFY(near(turned.rect.width(), size.height, 1.5) && near(turned.rect.height(), size.width, 1.5));
        QVERIFY(near((turned.rect.x0 + turned.rect.x1) / 2, 200 + size.width / 2, 1));
        doc->session().undo();
        QTRY_VERIFY_WITH_TIMEOUT(near(ot::annot::stampRotation(doc->session(), ref).result(), 0, 0.01),
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(select.stampAngle().has_value() && near(*select.stampAngle(), 0, 0.01),
                                 10000);
    }

    void attachToolEmbedsAndOpens() {
        OT_FEATURE("comment-14");
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        const QString file = dataDir() + QStringLiteral("/заметки.txt");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Привет, вложение!\n");
        }
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        int asked = 0;
        CommentMode::setFilePrompt([&] {
            ++asked;
            return file;
        });
        // Отмена выбора файла — ничего.
        CommentMode::setFilePrompt([] { return QString(); });
        commentTool().start(CommentAction::Attach);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 500, 200));
        QTest::qWait(50);
        QCOMPARE(countOf(*doc, AnnotType::FileAttachment), 0);
        CommentMode::setFilePrompt([&] {
            ++asked;
            return file;
        });
        commentTool().start(CommentAction::Attach);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 500, 200));
        QTRY_COMPARE_WITH_TIMEOUT(countOf(*doc, AnnotType::FileAttachment), 1, 10000);
        QCOMPARE(asked, 1);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Add attachment"));
        const AnnotInfo info = ofType(*doc, AnnotType::FileAttachment).front();
        const AnnotProps p = propsOf(*doc, info.ref);
        QCOMPARE(QString::fromStdString(p.contents), QStringLiteral("заметки.txt"));
        QVERIFY(near((p.rect.x0 + p.rect.x1) / 2, 500, 1) && near((p.rect.y0 + p.rect.y1) / 2, 200, 1));
        // /Rect значка — по /BBox вида MuPDF (как у записки), центр — точка щелчка.
        QVERIFY2(p.rect.width() > 10 && p.rect.width() <= CommentMode::kNoteIconSize + 0.5,
                 qPrintable(QStringLiteral("%1 × %2").arg(p.rect.width()).arg(p.rect.height())));

        // Двойной щелчок «Выбором объектов» — панель «Вложения»: подтверждение и копия во временной папке.
        auto* pane = qobject_cast<ot::ui::AttachmentsPane*>(w.navPane().page(QStringLiteral("attachments")));
        QVERIFY(pane != nullptr);
        QStringList confirmed;
        pane->setConfirm([&](const QString& name) {
            confirmed << name;
            return true;
        });
        v.tools().setTool(ViewToolId::AnnotSelect);
        v.tools().annotSelect().setSelection({info});
        const QPoint icon = at(v, 500, 200);
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, icon);
        QTRY_COMPARE_WITH_TIMEOUT(shell.calls.size(), 1, 10000);
        QCOMPARE(confirmed, QStringList{QStringLiteral("заметки.txt")});
        QCOMPARE(shell.calls[0].kind, QStringLiteral("openFile"));
        QVERIFY(shell.calls[0].target.endsWith(QStringLiteral("/заметки.txt")));
        QFile opened(shell.calls[0].target);
        QVERIFY(opened.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(opened.readAll()), QStringLiteral("Привет, вложение!\n"));
        QTRY_COMPARE(w.navPane().currentPane(), QStringLiteral("attachments"));
    }
};

OT_UI_TEST(StampsPaletteTest, "stamps_palette");
#include "stamps_palette_test.moc"
