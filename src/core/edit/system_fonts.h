// Системные шрифты для редактора текста (план 3.2, п. 2; эталон 3.9 «FontResolver», edit-49 — поиск шрифта):
// интерфейс SystemFonts в core, живая реализация — DirectWrite в services (services/system_fonts_dwrite.h,
// слой 5.1: COM и Windows — не в core), для тестов — FakeSystemFonts по таблице файлов %WINDIR%\Fonts.
//
// findSystemFont — «тот же шрифт, что в PDF»: по PostScript-имени грани (подмножество «ABCDEF+Arial-BoldMT»),
// затем по семейству из имени или /FontFamily (вес и курсив — ближайшая грань, недостающее — синтетика, как
// DWRITE_FONT_SIMULATIONS), затем метрически совместимая замена (Helvetica → Arial, Times → Times New Roman,
// Courier → Courier New — правило 4 для base14).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ot::edit {

// Грань системного шрифта.
struct SystemFace {
    std::string family;            // семейство («Arial»)
    std::string postscript;        // PostScript-имя грани («Arial-BoldMT»); может быть пустым
    std::filesystem::path file;    // файл шрифта
    int index = 0;                 // грань в коллекции .ttc
    int weight = 400;              // вес грани (100–900)
    bool italic = false;           // грань курсивная (italic или oblique)
    bool simulatedBold = false;    // нужен жирный, а такой грани нет — синтетика (Tr 2)
    bool simulatedItalic = false;  // нужен курсив, а грани нет — синтетика (наклон Tm)

    bool sameFile(const SystemFace& o) const { return file == o.file && index == o.index; }
};

class SystemFonts {
public:
    virtual ~SystemFonts() = default;
    // Грань семейства family (без учёта регистра), ближайшая к weight и italic; семейства нет — nullopt.
    virtual std::optional<SystemFace> face(std::string_view family, int weight, bool italic) = 0;
    // Грань по PostScript- или полному имени («ArialMT», «Arial Bold»); нет — nullopt.
    virtual std::optional<SystemFace> byName(std::string_view name) = 0;
    // Семейства по алфавиту (список «системные» панели FORMAT TEXT).
    virtual std::vector<std::string> families() = 0;
};

// Что ищем: шрифт пробега PDF или семейство из настроек.
struct FontQuery {
    std::string name;    // /BaseFont (с префиксом подмножества или без); может быть пустым
    std::string family;  // семейство (FontDetails::family); пусто — из name
    int weight = 400;
    bool italic = false;
    bool metricSubstitute = true;  // разрешена замена base14 и метрических клонов
};

std::optional<SystemFace> findSystemFont(SystemFonts& fonts, const FontQuery& query);

// Метрически совместимое семейство Windows для base14 и их клонов (Liberation, Nimbus, Arimo/Tinos/Cousine);
// нет замены — "". Регистр не важен.
std::string_view metricSubstitute(std::string_view family);

// Выбор грани среди faces одного семейства, как у DirectWrite: сначала курсив как просили, затем ближайший
// вес (при равенстве — до 400 легче, иначе тяжелее); жирный (≥ 600) без жирной грани и курсив без курсивной —
// simulatedBold / simulatedItalic. faces не пуст.
SystemFace pickFace(const std::vector<SystemFace>& faces, int weight, bool italic);

std::string lowerAscii(std::string_view s);

// Подделка для тестов: таблица известных граней (Arial, Times New Roman, Courier New, Segoe UI, Segoe UI
// Symbol, Calibri, Cambria, Georgia, Verdana, Tahoma, Symbol, Wingdings) — только те, чьи файлы есть в
// каталоге.
class FakeSystemFonts final : public SystemFonts {
public:
    explicit FakeSystemFonts(std::filesystem::path dir = defaultDir());
    static std::filesystem::path defaultDir();  // %WINDIR%\Fonts

    std::optional<SystemFace> face(std::string_view family, int weight, bool italic) override;
    std::optional<SystemFace> byName(std::string_view name) override;
    std::vector<std::string> families() override;

    // Как будто семейства нет / своя грань.
    void remove(std::string_view family);
    void add(SystemFace face);

private:
    std::vector<SystemFace> faces_;
};

}  // namespace ot::edit
