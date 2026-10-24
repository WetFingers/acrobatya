// Картинки страницы для «Редактировать PDF» (план 3.4, п. 3; эталон 3.9 «Инструмент», «Модель»: «Картинки —
// cm-матрица XObject, обрезка — W n перед Do, замена — pdf_add_image + подмена в /Resources/XObject»;
// edit-06, edit-07, create-32 — экспорт одной картинки).
//
// Картинка — операция Do XObject-изображения в потоках самой страницы (в формах — нет: их делят страницы);
// её место — CTM у Do (обход q/Q/cm модели контент-потока) × матрица страницы: единичный квадрат картинки →
// координаты страницы. Правка переписывает только место картинки в потоке своей «группой»:
//
//     q  a b c d e f cm  [x y w h re W n]  /Имя Do  Q
//
// cm — матрица единичного квадрата относительно CTM до группы, обтравка — в единицах картинки (0…1:
// двигается, масштабируется и поворачивается вместе с ней). Группа заменяет прежнюю группу той же формы (q cm
// [re W n] Do Q — как пишут Word и сам редактор) или одиночный Do; остальной поток — байт в байт.
// Перемещение, масштаб, поворот, отражение — новая матрица; обрезка — обтравка; замена — pdf_add_image с
// новым именем ресурса (OtImN) в /Resources/XObject страницы и этой группе; добавление — группа новым потоком
// в конце /Contents (если CTM в конце страницы не единичная — «q» новым первым потоком и «Q» перед группой);
// удаление — группа убирается. Каждая правка — одна операция журнала (markHeavy — полная запись). Экспорт:
// JPEG (DCTDecode) — байты как есть, прочее — PNG. Подписанный документ и /Perms — отказ
// (std::runtime_error), как у Rewriter. Всё — в потоке документа.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::edit {

class ContentModel;

struct PageImage {
    int index = 0;           // по порядку Do изображений на странице
    int op = -1;             // операция Do в модели
    std::string name;        // имя ресурса /XObject
    int objnum = 0;          // объект изображения
    Matrix matrix;           // единичный квадрат → пространство пользователя (CTM у Do)
    Matrix pageMatrix;       // единичный квадрат → координаты страницы
    QuadF quad;              // углы картинки на странице
    RectF bbox;              // видимая часть (с обтравкой) — координаты страницы
    RectF clip{0, 0, 1, 1};  // обтравка группы в единицах картинки (весь квадрат — нет)
    int width = 0;           // пиксели
    int height = 0;
    std::string filter;  // "DCTDecode", "FlateDecode"… (первый фильтр)
    bool group = false;  // место — группой q cm [re W n] Do Q
    bool clipped() const noexcept { return clip.x0 > 0 || clip.y0 > 0 || clip.x1 < 1 || clip.y1 < 1; }
};

// Картинка для буфера обмена и экспорта: закодированные байты и формат.
struct ImageData {
    std::string bytes;
    std::string format;  // "jpg", "png"
    int width = 0;
    int height = 0;
};

// Картинки страницы page модели model (для ui — services/edit_ops.h).
std::vector<PageImage> pageImages(const Document& doc, const ContentModel& model);
std::vector<PageImage> pageImages(const Document& doc, int page);
// Картинка под точкой страницы (верхняя — последняя по потоку); −1 — нет.
int imageAt(const std::vector<PageImage>& images, PointF pagePoint);

class ImageOps {
public:
    explicit ImageOps(Document& doc);

    std::vector<PageImage> list(int page) const;

    // Место картинки index страницы page: новая матрица единичного квадрата в координатах страницы.
    void setPageMatrix(int page, int index, const Matrix& unitToPage,
                       const std::string& operation = "Edit image");
    // Сдвиг на странице (координаты страницы).
    void move(int page, int index, PointF delta, const std::string& operation = "Move image");
    // Масштаб: видимая рамка картинки — в rect (координаты страницы), ориентация та же.
    void setRect(int page, int index, const RectF& rect, const std::string& operation = "Resize image");
    // Поворот вокруг центра видимой части на degrees (по часовой стрелке на экране — положительный).
    void rotate(int page, int index, float degrees, const std::string& operation = "Rotate image");
    // Отражение относительно центра: horizontal — слева направо, иначе сверху вниз (на экране).
    void flip(int page, int index, bool horizontal, const std::string& operation = "Flip image");
    // Обрезка: видимая часть — пересечение картинки с rect (координаты страницы); вся картинка — снять.
    void crop(int page, int index, const RectF& rect, const std::string& operation = "Crop image");
    // Замена картинки (файл PNG/JPEG/… байтами): то же место, новое изображение — новым ресурсом.
    void replace(int page, int index, std::string_view bytes, const std::string& operation = "Replace image");
    // Добавить картинку в rect (координаты страницы); возвращает её индекс.
    int add(int page, std::string_view bytes, const RectF& rect, const std::string& operation = "Add image");
    void remove(int page, int index, const std::string& operation = "Delete image");
    // Экспорт (create-32): DCTDecode — JPEG как есть, остальное — PNG (CMYK — в RGB).
    ImageData exportImage(int page, int index) const;

private:
    Document& doc_;
};

// Размер картинки (пиксели) по закодированным байтам; 0 × 0 — не картинка.
ImageData probeImage(std::string_view bytes);

}  // namespace ot::edit
