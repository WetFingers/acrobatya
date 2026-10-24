// Токены оформления (эталон 6.4) — единственное место цветов и размеров интерфейса. Две палитры таблицы
// эталона (светлая, тёмная) и третья — «Высокая контрастность» (ocr-45) из тех же имён; размеры — одна база и
// Tokens::scaled(k) для «Крупных элементов» (k = 1,25, ocr-42). В коде экранов цветов и чисел размеров нет.
#pragma once

#include <QColor>

#include <cstdint>

namespace ot::ui {

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    constexpr bool operator==(const Rgba&) const = default;
    QColor color() const { return QColor(r, g, b, a); }
};

// #RRGGBB и прозрачность 0…1 (rgba(…, .30) таблицы → 77).
constexpr Rgba hex(std::uint32_t rgb, double alpha = 1.0) {
    return Rgba{static_cast<std::uint8_t>((rgb >> 16) & 0xFF), static_cast<std::uint8_t>((rgb >> 8) & 0xFF),
                static_cast<std::uint8_t>(rgb & 0xFF), static_cast<std::uint8_t>(alpha * 255.0 + 0.5)};
}

enum class ThemeKind { Light, Dark, HighContrast };

struct Palette {
    Rgba surface;         // панели, диалоги
    Rgba surfaceAlt;      // глобальная панель, строка состояния
    Rgba canvas;          // холст вокруг страниц
    Rgba border;          // рамки, разделители
    Rgba text;            // текст
    Rgba textMuted;       // подписи
    Rgba accent;          // выбранный инструмент, главные кнопки, фокус
    Rgba accentSoft;      // выбранная строка
    Rgba onAccent;        // текст и иконки на accent (контраст ≥ 4,5:1)
    Rgba hover;           // наведение
    Rgba danger;          // ошибки, «подпись недействительна»
    Rgba warn;            // предупреждения, защищённый просмотр
    Rgba ok;              // «подписано», успех
    Rgba info;            // сведения, ссылки
    Rgba form;            // полоса «Документ содержит поля» (фиолетовая, эталон 6.1)
    Rgba formField;       // подсветка полей формы
    Rgba selection;       // выделение текста
    Rgba annotHighlight;  // маркер
    Rgba shadow;          // тень плавающей панели и диалогов (эталон 6.1: 0 4 16 rgba(0,0,0,.18))
};

inline constexpr Palette kLightPalette{
    .surface = hex(0xFFFFFF),
    .surfaceAlt = hex(0xF5F5F6),
    .canvas = hex(0xE6E6E8),
    .border = hex(0xD9D9DC),
    .text = hex(0x1F1F1F),
    .textMuted = hex(0x6E6E73),
    .accent = hex(0xB2432F),
    .accentSoft = hex(0xF6E3DF),
    .onAccent = hex(0xFFFFFF),
    .hover = hex(0xECECEE),
    .danger = hex(0xC9372C),
    .warn = hex(0xB7791F),
    .ok = hex(0x2E7D32),
    .info = hex(0x2B6CC4),
    .form = hex(0x7048B6),
    .formField = hex(0xDCE6FA),
    .selection = hex(0x2B6CC4, 0.30),
    .annotHighlight = hex(0xFFEB3B),
    .shadow = hex(0x000000, 0.18),
};

inline constexpr Palette kDarkPalette{
    .surface = hex(0x1F1F1F),
    .surfaceAlt = hex(0x2A2A2B),
    .canvas = hex(0x121212),
    .border = hex(0x3A3A3C),
    .text = hex(0xECECEC),
    .textMuted = hex(0xA4A4A8),
    .accent = hex(0xD9634E),
    .accentSoft = hex(0x4A2B26),
    .onAccent = hex(0x121212),  // белый на #D9634E — 3,6:1, тёмный — 5,2:1
    .hover = hex(0x333335),
    .danger = hex(0xF28B82),
    .warn = hex(0xF5C451),
    .ok = hex(0x7BD88F),
    .info = hex(0x5B9BF0),
    .form = hex(0xB49AF0),
    .formField = hex(0x2A3A5A),
    .selection = hex(0x5B9BF0, 0.35),
    .annotHighlight = hex(0xFFEB3B),
    .shadow = hex(0x000000, 0.40),
};

// «Высокая контрастность»: чёрный фон, белый текст, светлые рамки, бирюзовый акцент (как «Водная» тема
// Windows).
inline constexpr Palette kHighContrastPalette{
    .surface = hex(0x000000),
    .surfaceAlt = hex(0x0A0A0A),
    .canvas = hex(0x1A1A1A),
    .border = hex(0xC8C8C8),
    .text = hex(0xFFFFFF),
    .textMuted = hex(0xD8D8D8),
    .accent = hex(0x1AEBFF),
    .accentSoft = hex(0x00434A),
    .onAccent = hex(0x000000),
    .hover = hex(0x2E2E2E),
    .danger = hex(0xFF7B72),
    .warn = hex(0xFFD60A),
    .ok = hex(0x3FF23F),
    .info = hex(0x75D7FF),
    .form = hex(0xD6B4FD),
    .formField = hex(0x1A2F5A),
    .selection = hex(0x1AEBFF, 0.45),
    .annotHighlight = hex(0xFFEB3B),
    .shadow = hex(0x000000, 0.0),
};

// Цвета пометок (комментарии, маркер, подписи Fill & Sign) — данные документа, а не тема: одни во всех
// палитрах.
inline constexpr Rgba kAnnotationColors[] = {
    hex(0xFFEB3B), hex(0xFF9800), hex(0xF44336), hex(0xE91E63), hex(0x9C27B0), hex(0x3F51B5),
    hex(0x2196F3), hex(0x00BCD4), hex(0x4CAF50), hex(0x8BC34A), hex(0x000000), hex(0xFFFFFF),
};

// Бумага — фон страницы до прихода растра и под прозрачными страницами: цвет документа, не темы (в тёмной
// теме страница остаётся белой, как в Acrobat; ночной режим — замена цветов 2.5b).
inline constexpr Rgba kPaperColor = hex(0xFFFFFF);

// Замена цветов документа (2.5b; эталон 3.16; view-26, ocr-38, ocr-45): пары «текст / фон» — данные
// документа, а не тема (одни во всех палитрах; бумага при замене — цвет фона).
struct ReadingColors {
    Rgba text;
    Rgba background;
};
// «Ночной режим» — светлый текст на тёмном фоне.
inline constexpr ReadingColors kNightReading{hex(0xE6E6E6), hex(0x1E1E1E)};
// «Высокая контрастность» — сочетания Acrobat (Установки → Специальные возможности) в порядке ключа
// reading/highContrastScheme: зелёный на чёрном, жёлтый на чёрном, белый на чёрном, чёрный на белом.
inline constexpr ReadingColors kHighContrastReading[] = {
    {hex(0x00FF00), hex(0x000000)},
    {hex(0xFFFF00), hex(0x000000)},
    {hex(0xFFFFFF), hex(0x000000)},
    {hex(0x000000), hex(0xFFFFFF)},
};

// Рамки областей в виде (2.5b; Установки → Просмотр страницы): выпуск (/BleedBox), обрезка (/TrimBox),
// иллюстрация (/ArtBox) — данные документа, одни во всех палитрах.
inline constexpr Rgba kPageBoxColors[] = {hex(0x2196F3), hex(0xF44336), hex(0x4CAF50)};

// Размеры, px при k = 1 (эталон 6.4–6.5). motionMs — время, scaled() его не трогает.
struct Metrics {
    int grid = 4;                 // сетка
    int globalBar = 48;           // глобальная панель
    int tabHeight = 36;           // вкладка документа
    int tabMaxWidth = 220;        //   и её наибольшая ширина
    int tabMinWidth = 140;        //   и наименьшая (дальше — прокрутка ◀ ▶, 2.3)
    int searchWidth = 240;        // поле поиска глобальной панели (2.3)
    int row = 32;                 // строка списка
    int cardRow = 64;             // карточка комментария (минимум)
    int navButton = 44;           // кнопка правой навигации
    int leftPanel = 320;          // левая панель
    int leftPanelCollapsed = 48;  //   свёрнутая
    int rightPanel = 280;         // правая панель
    int rightPanelMin = 220;      //   ширина тянется 220–480
    int rightPanelMax = 480;
    int centerMin = 240;         // документ между панелями не уже (Главная его не расширяет, 2.4)
    int statusBar = 32;          // строка состояния
    int radius = 4;              // скругление кнопок и полей
    int radiusLarge = 8;         // карточки, плавающая панель
    int hitTarget = 32;          // зона нажатия не меньше
    int iconPanel = 20;          // иконка в панелях
    int iconRow = 16;            // иконка в строках и меню
    int iconEmpty = 40;          // иконка пустого состояния
    int panelTitlePx = 13;       // заголовок панели, 600
    int homeTitlePx = 20;        // заголовок Главной
    int homeNav = 240;           // левый столбец Главной: Недавние, Избранное, Файлы, Сканы (2.3)
    int cardWidth = 184;         // карточка файла Главной: миниатюра 160 + поля
    int cardHeight = 256;        //   и её высота (миниатюра до 200 + имя и дата)
    int thumbWidth = 160;        // миниатюра первой страницы (эталон 3.18)
    int focusRing = 2;           // рамка фокуса accent
    int shadowBlur = 16;         // тень: размытие
    int shadowOffset = 4;        //   и сдвиг вниз
    int pageGap = 8;             // зазор между страницами и до края холста (эталон 3.1)
    int toolWindowWidth = 280;   // окно «Лупы» и «Панорамирования и масштаба» (эталон 3.1)
    int toolWindowHeight = 220;  //   и его высота
    int handle = 8;              // ручка рамки выбранного объекта на странице (AnnotSelectTool)
    int dialogWidth = 880;       // диалог с категориями слева: «Установки» (2.4b)
    int dialogHeight = 600;      //   и его высота
    int dialogNav = 220;         //   список категорий
    int paletteWidth = 560;      // палитра команд (Ctrl+Shift+P, 2.4b)
    int printPreview = 320;      // предпросмотр диалога «Печать» — не уже (2.6)
    int paletteRows = 10;        //   строк списка без прокрутки (не размер — scaled() не трогает)
    int pageBorder = 1;          // рамка страницы на холсте — волосяная: «Крупные элементы» её не меняют
    double fontPt = 9.0;         // Segoe UI 9 pt
    int motionMs = 120;          // движение
    double scale = 1.0;          // k, с которым получены размеры
};

// Все поля-размеры Metrics: их масштабирует Tokens::scaled (и проверяет ui.theme).
inline constexpr int Metrics::* kMetricSizes[] = {
    &Metrics::grid,
    &Metrics::globalBar,
    &Metrics::tabHeight,
    &Metrics::tabMaxWidth,
    &Metrics::tabMinWidth,
    &Metrics::searchWidth,
    &Metrics::row,
    &Metrics::cardRow,
    &Metrics::navButton,
    &Metrics::leftPanel,
    &Metrics::leftPanelCollapsed,
    &Metrics::rightPanel,
    &Metrics::rightPanelMin,
    &Metrics::rightPanelMax,
    &Metrics::centerMin,
    &Metrics::statusBar,
    &Metrics::radius,
    &Metrics::radiusLarge,
    &Metrics::hitTarget,
    &Metrics::iconPanel,
    &Metrics::iconRow,
    &Metrics::iconEmpty,
    &Metrics::panelTitlePx,
    &Metrics::homeTitlePx,
    &Metrics::homeNav,
    &Metrics::cardWidth,
    &Metrics::cardHeight,
    &Metrics::thumbWidth,
    &Metrics::focusRing,
    &Metrics::shadowBlur,
    &Metrics::shadowOffset,
    &Metrics::pageGap,
    &Metrics::toolWindowWidth,
    &Metrics::toolWindowHeight,
    &Metrics::handle,
    &Metrics::dialogWidth,
    &Metrics::dialogHeight,
    &Metrics::dialogNav,
    &Metrics::paletteWidth,
    &Metrics::printPreview,
};

struct Tokens {
    static constexpr Metrics base() { return Metrics{}; }

    // «Крупные элементы»: k = 1,0 или 1,25 — одна функция, не второй набор размеров.
    static constexpr Metrics scaled(double k) {
        Metrics m;
        for (int Metrics::* field : kMetricSizes) {
            m.*field = static_cast<int>(m.*field * k + 0.5);
        }
        m.fontPt *= k;
        m.scale = k;
        return m;
    }

    static constexpr const Palette& palette(ThemeKind kind) {
        switch (kind) {
            case ThemeKind::Dark:
                return kDarkPalette;
            case ThemeKind::HighContrast:
                return kHighContrastPalette;
            case ThemeKind::Light:
                break;
        }
        return kLightPalette;
    }
};

}  // namespace ot::ui
