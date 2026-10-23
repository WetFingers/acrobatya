// Сцены вида страниц (план 2.2b; эталон 3.1, 3.2, 6.10): PageView на фикстуре во весь кадр 1440 × 900 — вид
// ещё не в окне (его ставит 2.3, тогда сцены перейдут в главное окно). Раскладки (одна страница, непрерывно,
// разворот, титул отдельно), поворот вида, 400 %, тёмная тема (холст тёмный, бумага белая), выделение и
// совпадения поиска, «Лупа». Режим, масштаб и прокрутка ставятся после показа (нужен размер вьюпорта); кадр
// снимается, когда все видимые тайлы готовы (Scene::ready).
#include <QCoreApplication>
#include <QResizeEvent>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "core/text/search.h"
#include "core/text/selection.h"
#include "core/text/stext.h"
#include "services/document_session.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/tool_panels.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

namespace {

// Окно сцены: свой DocumentSession и PageView во весь размер. Вид удаляется раньше сессии.
class ViewSceneHost : public QWidget {
public:
    using Setup = std::function<void(ViewSceneHost&)>;
    using Ready = std::function<bool(ViewSceneHost&)>;

    ViewSceneHost(const QString& fixture, Setup setup, Ready extra)
        : session_(std::make_unique<DocumentSession>()), setup_(std::move(setup)), extra_(std::move(extra)) {
        setObjectName(QStringLiteral("viewScene"));
        view_ = new PageView(this);
        const QString path = sceneFixture(fixture);
        if (!path.isEmpty() && session_->open(path)) {
            view_->setDocument(session_.get());
        }
    }
    ~ViewSceneHost() override { delete view_; }
    ViewSceneHost(const ViewSceneHost&) = delete;
    ViewSceneHost& operator=(const ViewSceneHost&) = delete;

    PageView& view() { return *view_; }
    DocumentSession& session() { return *session_; }

    bool ready() {
        if (!setupDone_ || view_->pageCount() == 0) {
            return false;
        }
        view_->viewport()->repaint();
        return view_->tilePainter().visibleComplete() && (!extra_ || extra_(*this));
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        view_->setGeometry(rect());
    }
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (!setupPosted_) {
            setupPosted_ = true;
            // После показа: размер вьюпорта известен — подгонки и прокрутка верные.
            QTimer::singleShot(0, this, [this] {
                view_->setFocus();
                if (setup_) {
                    setup_(*this);
                }
                setupDone_ = true;
            });
        }
    }

private:
    std::unique_ptr<DocumentSession> session_;
    PageView* view_ = nullptr;
    Setup setup_;
    ViewSceneHost::Ready extra_;
    bool setupPosted_ = false;
    bool setupDone_ = false;
};

Scene viewScene(const char* name, const char* fixture, ViewSceneHost::Setup setup,
                std::optional<ThemeChoice> theme = {}, ViewSceneHost::Ready extra = {}) {
    Scene s;
    s.name = QLatin1String(name);
    const QString file = QLatin1String(fixture);
    s.make = [file, setup = std::move(setup), extra = std::move(extra)]() -> QWidget* {
        return new ViewSceneHost(file, setup, extra);
    };
    s.theme = theme;
    s.ready = [](QWidget* w) { return static_cast<ViewSceneHost*>(w)->ready(); };
    return s;
}

PointF scenePoint(double x, double y) { return {static_cast<float>(x), static_cast<float>(y)}; }

// Две строки выделены (как протягиванием «Выделения») и первое слово страницы («Оттиск» заголовка) найдено:
// подсветка всех совпадений, второе — текущее. Искомое берётся из документа — строк с кириллицей в коде нет.
void selectionSetup(ViewSceneHost& h) {
    PageView& v = h.view();
    v.setZoomPercent(150);
    v.goToPagePoint(0, QPointF(40, 40));
    try {
        const auto text = h.session().textPage(0).result();
        if (text) {
            const TextSelection sel = selectStream(*text, scenePoint(72.5, 257), scenePoint(250, 274));
            v.selectionOverlay().setQuads(0, sel.quads);
            const auto lean = h.session().textPage(0, SearchSession::textOptions()).result();
            const std::vector<TextWord> words = text->words();
            if (!words.empty()) {
                std::string needle = words.front().text;
                while (!needle.empty() &&
                       std::string_view(":;,.").find(needle.back()) != std::string_view::npos) {
                    needle.pop_back();  // слово без знака препинания за ним
                }
                v.setSearchHits(searchPage(*lean, needle));
                v.setCurrentSearchHit(1);
            }
        }
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без слоя — сцена без подсветки
    }
}

}  // namespace

std::vector<Scene> viewScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(viewScene("view-single", "demo/report.pdf", [](ViewSceneHost& h) {
        h.view().setPageMode(PageMode::SinglePage);
        h.view().goToPage(1);
        h.view().fit(FitMode::Page);
    }));
    scenes.push_back(viewScene("view-continuous", "demo/report.pdf", [](ViewSceneHost& h) {
        h.view().setPageMode(PageMode::Continuous);
        h.view().setZoomPercent(100);
        h.view().goToPage(1);
        h.view().scrollLines(14);  // низ страницы 2 и верх страницы 3 — видны зазор и рамки
    }));
    scenes.push_back(viewScene("view-facing", "demo/report.pdf", [](ViewSceneHost& h) {
        h.view().setPageMode(PageMode::Facing);
        h.view().setCoverPage(false);
        h.view().goToPage(2);
        h.view().fit(FitMode::Page);
    }));
    scenes.push_back(viewScene("view-cover", "demo/report.pdf", [](ViewSceneHost& h) {
        h.view().setPageMode(PageMode::FacingContinuous);
        h.view().setCoverPage(true);
        h.view().setZoomPercent(50);
        h.view().goToPage(0);
    }));
    scenes.push_back(viewScene("view-rotated", "view/layout-mixed.pdf", [](ViewSceneHost& h) {
        h.view().setPageMode(PageMode::Continuous);
        h.view().setViewRotation(90);
        h.view().setZoomPercent(50);
        h.view().goToPage(0);
    }));
    scenes.push_back(viewScene("view-zoom-400", "text/search.pdf", [](ViewSceneHost& h) {
        h.view().setZoomPercent(400);
        h.view().goToPagePoint(0, QPointF(60, 60));
    }));
    scenes.push_back(viewScene(
        "view-dark", "demo/report.pdf",
        [](ViewSceneHost& h) {
            h.view().setPageMode(PageMode::Continuous);
            h.view().setZoomPercent(75);
            h.view().goToPage(0);
            h.view().scrollLines(10);
        },
        ThemeChoice::Dark));
    scenes.push_back(viewScene("view-selection", "text/search.pdf", selectionSetup));
    scenes.push_back(viewScene(
        "view-loupe", "text/search.pdf",
        [](ViewSceneHost& h) {
            h.view().setZoomPercent(100);
            h.view().goToPage(0);
            h.view().tools().setTool(ViewToolId::Loupe);
            h.view().tools().loupe().setCenter(0, QPointF(150, 258));
        },
        {},
        [](ViewSceneHost& h) {
            const LoupePanel* panel = h.view().tools().loupe().panel();
            return panel != nullptr && !panel->image().isNull();
        }));
    return scenes;
}

}  // namespace ot::ui
