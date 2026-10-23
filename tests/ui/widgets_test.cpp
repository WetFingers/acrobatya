// ui.widgets — компоненты src/ui/widgets (эталон 6.4–6.6): клики и сигналы, состояния, размеры из токенов,
// контраст каждого компонента в трёх палитрах (пары QSS из theme/qss.cpp), смена темы перекрашивает всю
// витрину; снимки витрины — build/<пресет>/ui-shots/.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QGraphicsDropShadowEffect>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cstdio>
#include <set>

#include "support/contrast.h"
#include "support/ui_test.h"
#include "ui/theme/theme.h"
#include "ui/widgets/card.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/floating_bar.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/panel_header.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/showcase.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"
#include "ui/widgets/zoom_box.h"

using namespace ot::ui;

namespace {

QApplication& app() { return *qobject_cast<QApplication*>(QCoreApplication::instance()); }

void saveShot(QWidget* w, const QString& name) {
    const QString dir = QStringLiteral(OTTISK_BINARY_DIR "/ui-shots");
    QDir().mkpath(dir);
    w->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

}  // namespace

class WidgetsTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Theme::apply(app(), ThemeChoice::Light); }

    void toolButton() {
        ToolButton b(QStringLiteral("printer"), QStringLiteral("Печать"));
        QCOMPARE(b.toolTip(), QStringLiteral("Печать"));
        QCOMPARE(b.accessibleName(), QStringLiteral("Печать"));
        QCOMPARE(b.iconSize(), QSize(20, 20));
        QVERIFY(b.minimumWidth() >= 32 && b.minimumHeight() >= 32);
        QSignalSpy clicked(&b, &QToolButton::clicked);
        b.show();
        QTest::mouseClick(&b, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        b.setCheckable(true);
        QTest::mouseClick(&b, Qt::LeftButton);
        QVERIFY(b.isChecked());
        b.setIconSizeKind(ToolButton::IconSize::Row);
        QCOMPARE(b.iconSize(), QSize(16, 16));
        Theme::apply(app(), ThemeChoice::Light, 1.25);
        QCOMPARE(b.iconSize(), QSize(20, 20));
        QCOMPARE(b.minimumHeight(), 40);
    }

    void segmentedBar() {
        SegmentedBar bar;
        QSignalSpy changed(&bar, &SegmentedBar::currentChanged);
        bar.addSegment(QStringLiteral("Одна"));
        bar.addSegment(QStringLiteral("Непрерывно"));
        bar.addSegment(QString(), QStringLiteral("layout-grid"), QStringLiteral("Сетка"));
        QCOMPARE(bar.count(), 3);
        QCOMPARE(bar.currentIndex(), 0);
        bar.show();
        changed.clear();
        QTest::mouseClick(bar.segment(1), Qt::LeftButton);
        QCOMPARE(bar.currentIndex(), 1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toInt(), 1);
        QVERIFY(!bar.segment(0)->isChecked());
        QCOMPARE(bar.segment(2)->accessibleName(), QStringLiteral("Сетка"));
        bar.setSegmentEnabled(2, false);
        QTest::mouseClick(bar.segment(2), Qt::LeftButton);
        QCOMPARE(bar.currentIndex(), 1);
        bar.setCurrentIndex(0);
        QCOMPARE(changed.count(), 2);
    }

    void searchField() {
        SearchField f;
        f.show();
        QCOMPARE(f.placeholderText(), QStringLiteral("Поиск"));
        QSignalSpy search(&f, &SearchField::searchRequested);
        QSignalSpy previous(&f, &SearchField::previousRequested);
        QSignalSpy cleared(&f, &SearchField::cleared);
        f.setText(QStringLiteral("договор"));  // QTest::keyClicks печатает только Latin-1
        QTest::keyClick(&f, Qt::Key_Return);
        QCOMPARE(search.count(), 1);
        QCOMPARE(search.at(0).at(0).toString(), QStringLiteral("договор"));
        QTest::keyClick(&f, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(previous.count(), 1);
        QTest::keyClick(&f, Qt::Key_Escape);
        QVERIFY(f.text().isEmpty());
        QCOMPARE(cleared.count(), 1);
        QTest::keyClick(&f, Qt::Key_Return);
        QCOMPARE(search.count(), 1);  // пустое не ищем
    }

    void panelHeader() {
        PanelHeader h(QStringLiteral("Комментарии"));
        h.show();
        QCOMPARE(h.title(), QStringLiteral("Комментарии"));
        QVERIFY(!h.backButton()->isVisible());
        h.setBackText(QStringLiteral("Все инструменты"));
        QVERIFY(h.backButton()->isVisible());
        QSignalSpy back(&h, &PanelHeader::backClicked);
        QTest::mouseClick(h.backButton(), Qt::LeftButton);
        QCOMPARE(back.count(), 1);
        ToolButton* more = h.addAction(QStringLiteral("ellipsis"), QStringLiteral("Ещё"));
        QCOMPARE(more->toolTip(), QStringLiteral("Ещё"));
        auto* title = h.findChild<QLabel*>(QStringLiteral("panelHeaderTitle"));
        QCOMPARE(title->font().pixelSize(), 13);
    }

    void card() {
        Card c(QStringLiteral("Договор.pdf"), QStringLiteral("12 страниц"), QStringLiteral("file-text"));
        c.show();
        QSignalSpy clicked(&c, &Card::clicked);
        QTest::mouseClick(&c, Qt::LeftButton);
        QCOMPARE(clicked.count(), 0);  // не кликабельная
        c.setClickable(true);
        QTest::mouseClick(&c, Qt::LeftButton);
        QCOMPARE(clicked.count(), 1);
        c.setFocus();
        QTest::keyClick(&c, Qt::Key_Return);
        QCOMPARE(clicked.count(), 2);
        c.setSelected(true);
        QVERIFY(c.property("selected").toBool());
        QCOMPARE(c.accessibleName(), QStringLiteral("Договор.pdf"));
    }

    void listRow() {
        ListRow r(QStringLiteral("Добавить комментарии"), QStringLiteral("message-square"));
        r.resize(300, 32);
        r.show();
        QCOMPARE(r.sizeHint().height(), 32);
        QSignalSpy clicked(&r, &ListRow::clicked);
        QSignalSpy pinned(&r, &ListRow::pinToggled);
        QTest::mouseClick(&r, Qt::LeftButton, {}, QPoint(40, 16));
        QCOMPARE(clicked.count(), 1);
        r.setPinnable(true);
        QVERIFY(r.pinButton()->isVisible());
        QTest::mouseClick(r.pinButton(), Qt::LeftButton);
        QCOMPARE(pinned.count(), 1);
        QVERIFY(r.isPinned());
        r.setSelected(true);
        QVERIFY(r.isSelected());
        QCOMPARE(r.accessibleName(), QStringLiteral("Добавить комментарии"));
        const QImage img = r.grab().toImage();
        QVERIFY(!img.isNull());
    }

    void chip() {
        Chip c(QStringLiteral("Подписано"), Tone::Ok);
        c.show();
        QCOMPARE(c.property("tone").toString(), QStringLiteral("ok"));
        c.setClosable(true);
        QSignalSpy closeReq(&c, &Chip::closeRequested);
        QTest::mouseClick(c.closeButton(), Qt::LeftButton);
        QCOMPARE(closeReq.count(), 1);
        Chip filter(QStringLiteral("Мои"));
        filter.show();
        QSignalSpy toggled(&filter, &Chip::toggled);
        QTest::mouseClick(&filter, Qt::LeftButton);
        QCOMPARE(toggled.count(), 0);  // не фильтр
        filter.setCheckable(true);
        QTest::mouseClick(&filter, Qt::LeftButton);
        QCOMPARE(toggled.count(), 1);
        QVERIFY(filter.isChecked());
        QTest::keyClick(&filter, Qt::Key_Space);
        QVERIFY(!filter.isChecked());
        QCOMPARE(filter.height(), 24);
    }

    void toast() {
        QWidget host;
        host.resize(800, 600);
        host.show();
        QSignalSpy* closed = nullptr;
        Toast* t = Toast::notify(&host, QStringLiteral("Не удалось сохранить"), Tone::Danger,
                                 QStringLiteral("Подробнее"), 80);
        closed = new QSignalSpy(t, &Toast::closed);
        QVERIFY(t->isVisible());
        QCOMPARE(t->property("tone").toString(), QStringLiteral("danger"));
        QVERIFY(t->geometry().bottom() < host.height());
        QVERIFY(qAbs(t->geometry().center().x() - host.width() / 2) <= 1);
        QTRY_COMPARE_WITH_TIMEOUT(closed->count(), 1, 2000);
        delete closed;
        Toast persistent(QStringLiteral("Сведения"), Tone::Info, &host);
        persistent.setActionText(QStringLiteral("Подробнее"));
        persistent.setDuration(0);
        persistent.popup();
        QSignalSpy action(&persistent, &Toast::actionTriggered);
        QTest::mouseClick(persistent.findChild<QPushButton*>(QStringLiteral("toastAction")), Qt::LeftButton);
        QCOMPARE(action.count(), 1);
        QCOMPARE(Toast::kDefaultMs, 6000);
        QVERIFY(persistent.graphicsEffect() != nullptr);
    }

    void emptyState() {
        EmptyState e(QStringLiteral("inbox"), QStringLiteral("Комментариев пока нет"),
                     QStringLiteral("Добавить"));
        e.show();
        QSignalSpy action(&e, &EmptyState::actionTriggered);
        QTest::mouseClick(e.actionButton(), Qt::LeftButton);
        QCOMPARE(action.count(), 1);
        e.setActionText(QString());
        QVERIFY(!e.actionButton()->isVisible());
        QCOMPARE(e.text(), QStringLiteral("Комментариев пока нет"));
    }

    void messageBar() {
        MessageBar bar(Tone::Form, QStringLiteral("Документ содержит поля"));
        bar.show();
        QCOMPARE(bar.property("tone").toString(), QStringLiteral("form"));
        QPushButton* b = bar.addAction(QStringLiteral("Подсветить поля"), QStringLiteral("highlight"), true);
        QVERIFY(b->property("primary").toBool());
        QSignalSpy action(&bar, &MessageBar::actionTriggered);
        QSignalSpy closed(&bar, &MessageBar::closed);
        QTest::mouseClick(b, Qt::LeftButton);
        QCOMPARE(action.count(), 1);
        QCOMPARE(action.at(0).at(0).toString(), QStringLiteral("highlight"));
        bar.setTone(Tone::Warn);
        QCOMPARE(bar.property("tone").toString(), QStringLiteral("warn"));
        QTest::mouseClick(bar.closeButton(), Qt::LeftButton);
        QCOMPARE(closed.count(), 1);
        QVERIFY(!bar.isVisible());
    }

    void colorSwatch() {
        ColorSwatch s;
        s.show();
        QCOMPARE(s.color(), kAnnotationColors[0].color());
        QSignalSpy changed(&s, &ColorSwatch::colorChanged);
        s.setColor(Qt::red);
        QCOMPARE(changed.count(), 0);  // программная смена — без сигнала
        QCOMPARE(s.presetButtons().size(), std::size(kAnnotationColors));
        s.presetButtons()[6]->click();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(s.color(), kAnnotationColors[6].color());
        QVERIFY(s.paletteMenu() != nullptr);
    }

    void numberField() {
        NumberField n;
        n.setLocale(QLocale(QLocale::Russian, QLocale::Russia));
        n.setRange(0, 100);
        n.setDecimals(1);
        n.setUnit(QStringLiteral("пт"));
        n.show();
        n.setFocus();
        QSignalSpy changed(&n, &QDoubleSpinBox::valueChanged);
        n.selectAll();
        QTest::keyClicks(&n, QStringLiteral("12,5"));
        QCOMPARE(changed.count(), 0);  // до Enter значение не меняется
        QTest::keyClick(&n, Qt::Key_Return);
        QCOMPARE(n.value(), 12.5);
        QCOMPARE(changed.count(), 1);
        n.selectAll();
        QTest::keyClicks(&n, QStringLiteral("7.5"));
        QTest::keyClick(&n, Qt::Key_Return);
        QCOMPARE(n.value(), 7.5);
        QVERIFY(n.text().endsWith(QStringLiteral(" пт")));
        n.setValue(1000);
        QCOMPARE(n.value(), 100.0);
        n.setFixedWidth(96);
        QApplication::processEvents();
        auto* edit = n.findChild<QLineEdit*>();
        QVERIFY2(edit->width() > 60, qPrintable(QString::number(edit->width())));  // поле ввода не сжато QSS
    }

    void propertySheet() {
        PropertySheet sheet;
        sheet.addSection(QStringLiteral("Внешний вид"));
        auto* width = new NumberField();
        QLabel* label = sheet.addRow(QStringLiteral("&Толщина"), width);
        sheet.addRow(QStringLiteral("&Цвет"), new ColorSwatch());
        QCOMPARE(sheet.rowCount(), 2);
        QCOMPARE(label->buddy(), static_cast<QWidget*>(width));
        QCOMPARE(sheet.editorAt(0), static_cast<QWidget*>(width));
        QCOMPARE(width->accessibleName(), QStringLiteral("Толщина"));
        QVERIFY(width->parentWidget() == &sheet);
    }

    void zoomBox() {
        ZoomBox z;
        z.show();
        QSignalSpy zoom(&z, &ZoomBox::zoomRequested);
        QSignalSpy fit(&z, &ZoomBox::fitRequested);
        z.setZoomPercent(125);
        QCOMPARE(zoom.count(), 0);
        QVERIFY(z.combo()->currentText().startsWith(QStringLiteral("125")));
        QVERIFY(z.combo()->currentText().endsWith(QStringLiteral(" %")));
        QTest::mouseClick(z.zoomInButton(), Qt::LeftButton);
        QCOMPARE(zoom.last().at(0).toDouble(), 150.0);
        QTest::mouseClick(z.zoomOutButton(), Qt::LeftButton);
        QTest::mouseClick(z.zoomOutButton(), Qt::LeftButton);
        QCOMPARE(zoom.last().at(0).toDouble(), 100.0);
        z.combo()->lineEdit()->selectAll();
        QTest::keyClicks(z.combo()->lineEdit(), QStringLiteral("250"));
        QTest::keyClick(z.combo()->lineEdit(), Qt::Key_Return);
        QCOMPARE(zoom.last().at(0).toDouble(), 250.0);
        z.combo()->lineEdit()->selectAll();
        QTest::keyClicks(z.combo()->lineEdit(), QStringLiteral("99999"));
        QTest::keyClick(z.combo()->lineEdit(), Qt::Key_Return);
        QCOMPARE(zoom.last().at(0).toDouble(), 6400.0);
        QVERIFY(!z.zoomInButton()->isEnabled());
        const int widthIndex = z.combo()->findText(QStringLiteral("По ширине"));
        QVERIFY(widthIndex >= 0);
        emit z.combo()->activated(widthIndex);
        QCOMPARE(fit.count(), 1);
        QCOMPARE(fit.at(0).at(0).value<ZoomBox::Fit>(), ZoomBox::Fit::Width);
        QCOMPARE(ZoomBox::parsePercent(QStringLiteral("12,5 %")).value(), 12.5);
        QVERIFY(!ZoomBox::parsePercent(QStringLiteral("abc")).has_value());
        QCOMPARE(ZoomBox::parsePercent(QStringLiteral("0,1")).value(), 1.0);
        QCOMPARE(ZoomBox::stepPreset(6400, +1), 6400.0);
        QCOMPARE(ZoomBox::stepPreset(10, -1), 10.0);
        QCOMPARE(ZoomBox::stepPreset(110, -1), 100.0);
    }

    // Основа cloud-38: плавающая панель с настраиваемым составом и порядком, один выбранный инструмент, тень.
    void floatingBar() {
        FloatingBar bar;
        bar.addButton(QStringLiteral("select"), QStringLiteral("mouse-pointer-2"),
                      QStringLiteral("Выделение"));
        bar.addButton(QStringLiteral("hand"), QStringLiteral("hand"), QStringLiteral("Рука"));
        bar.addButton(QStringLiteral("note"), QStringLiteral("sticky-note"), QStringLiteral("Записка"));
        bar.show();
        QSignalSpy triggered(&bar, &FloatingBar::triggered);
        QTest::mouseClick(bar.button(QStringLiteral("hand")), Qt::LeftButton);
        QCOMPARE(triggered.count(), 1);
        QCOMPARE(triggered.at(0).at(0).toString(), QStringLiteral("hand"));
        QCOMPARE(bar.current(), QStringLiteral("hand"));
        bar.setCurrent(QStringLiteral("note"));
        QCOMPARE(bar.current(), QStringLiteral("note"));
        QVERIFY(!bar.button(QStringLiteral("hand"))->isChecked());
        bar.setOrder({QStringLiteral("note"), QStringLiteral("|"), QStringLiteral("select"),
                      QStringLiteral("unknown")});
        QCOMPARE(bar.order(),
                 (QStringList{QStringLiteral("note"), QStringLiteral("|"), QStringLiteral("select")}));
        QVERIFY(!bar.button(QStringLiteral("hand"))->isVisible());
        QVERIFY(bar.button(QStringLiteral("note"))->x() < bar.button(QStringLiteral("select"))->x());
        auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(bar.graphicsEffect());
        QVERIFY(shadow != nullptr);
        QCOMPARE(shadow->blurRadius(), 16.0);
        QCOMPARE(shadow->offset(), QPointF(0, 4));
        Theme::apply(app(), ThemeChoice::HighContrast);
        QVERIFY(bar.graphicsEffect() == nullptr);  // в высокой контрастности — рамка без тени
        std::printf("OT_FEATURE:cloud-38\n");
    }

    // Контраст каждого компонента: все 15 есть в парах QSS и проходят 4,5:1 во всех палитрах.
    void contrastOfEveryComponent() {
        const QStringList components = {
            QStringLiteral("ToolButton"),    QStringLiteral("SegmentedBar"), QStringLiteral("SearchField"),
            QStringLiteral("PanelHeader"),   QStringLiteral("Card"),         QStringLiteral("ListRow"),
            QStringLiteral("Chip"),          QStringLiteral("Toast"),        QStringLiteral("EmptyState"),
            QStringLiteral("MessageBar"),    QStringLiteral("ColorSwatch"),  QStringLiteral("NumberField"),
            QStringLiteral("PropertySheet"), QStringLiteral("ZoomBox"),      QStringLiteral("FloatingBar"),
        };
        for (ThemeKind kind : {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast}) {
            std::set<QString> seen;
            for (const ColorPair& pair : componentColors(Tokens::palette(kind))) {
                seen.insert(pair.component);
                const double ratio = ot::test::contrastRatio(pair.fg, pair.bg);
                QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 %2 %3: %4")
                                                      .arg(static_cast<int>(kind))
                                                      .arg(pair.component, pair.state)
                                                      .arg(ratio, 0, 'f', 2)));
            }
            for (const QString& c : components) {
                QVERIFY2(seen.count(c) == 1, qPrintable(c));
            }
        }
        // QSS действительно подключён: цвет текста выбранной кнопки в палитре виджета — из токена.
        ToolButton b(QStringLiteral("printer"), QStringLiteral("Печать"));
        b.show();
        QCOMPARE(b.palette().color(QPalette::ButtonText), Tokens::palette(ThemeKind::Light).text.color());
    }

    // Theme::apply меняет палитру всех виджетов витрины; снимки — build/<пресет>/ui-shots.
    void showcaseFollowsTheme() {
        std::unique_ptr<QWidget> page(createShowcase());
        page->resize(1440, 900);
        page->show();
        QVERIFY(QTest::qWaitForWindowExposed(page.get()));
        saveShot(page.get(), QStringLiteral("showcase-light"));
        const QList<QWidget*> all = page->findChildren<QWidget*>();
        QVERIFY(all.size() > 100);
        QHash<QWidget*, QColor> before;
        for (QWidget* w : all) {
            before.insert(w, w->palette().color(QPalette::WindowText));
        }
        Theme::apply(app(), ThemeChoice::Dark);
        QApplication::processEvents();
        saveShot(page.get(), QStringLiteral("showcase-dark"));
        int unchanged = 0;
        for (QWidget* w : all) {
            if (w->palette().color(QPalette::WindowText) == before.value(w)) {
                ++unchanged;
                qWarning("unchanged palette: %s %s", w->metaObject()->className(),
                         qPrintable(w->objectName()));
            }
        }
        QCOMPARE(unchanged, 0);
        QCOMPARE(page->palette().color(QPalette::Window), Tokens::palette(ThemeKind::Dark).surface.color());
        Theme::apply(app(), ThemeChoice::HighContrast);
        saveShot(page.get(), QStringLiteral("showcase-contrast"));
        Theme::apply(app(), ThemeChoice::Light, 1.25);
        std::unique_ptr<QWidget> large(createShowcase());
        large->resize(1440, 900);
        large->show();
        saveShot(large.get(), QStringLiteral("showcase-large"));
    }

    void showcaseWindowSwitchesTheme() {
        std::unique_ptr<QWidget> window(createShowcaseWindow());
        window->show();
        auto* themes = window->findChild<SegmentedBar*>();
        QVERIFY(themes != nullptr);
        themes->setCurrentIndex(1);
        QCOMPARE(Theme::instance().kind(), ThemeKind::Dark);
        QVERIFY(window->findChild<QWidget*>(QStringLiteral("showcase")) != nullptr);
    }
};

OT_UI_TEST(WidgetsTest, "widgets");
#include "widgets_test.moc"
