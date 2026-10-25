// unit.saved_signatures — сохранённые подпись и инициалы Fill & Sign и обработка их картинок (план 5.3, п. 1;
// эталон 3.5, 4.7 `signatures\`; sign-01, sign-02): PNG с альфой в
// <данные>/signatures/{signature,initials}.png переживает «перезапуск» (новый экземпляр службы), удаляется;
// белый фон картинки по порогу яркости уходит в прозрачность (цвет — исходный или чернил), обрезка по
// чернилам, перекраска, счёт связных штрихов.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "core/util/features.h"
#include "services/app_info.h"
#include "services/saved_signatures.h"
#include "services/signature_image.h"
#include "support/qt_app.h"

namespace fillsign = ot::fillsign;
using fillsign::SavedSignatures;
using fillsign::SignatureKind;

namespace {

// Три тёмных штриха на прозрачном (как нарисованная подпись).
QImage strokes() {
    QImage image(120, 40, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int s = 0; s < 3; ++s) {
        for (int x = 10 + s * 35; x < 35 + s * 35; ++x) {
            for (int y = 18; y < 22; ++y) {
                image.setPixelColor(x, y, QColor(0, 0, 0, 255));
            }
        }
    }
    return image;
}

// Скан подписи: белая бумага, светло-серый шум, синяя подпись.
QImage scanned() {
    QImage image(100, 50, QImage::Format_RGB32);
    image.fill(QColor(250, 250, 248));
    for (int x = 0; x < 100; x += 7) {
        image.setPixelColor(x, 3, QColor(235, 235, 235));  // шум светлее порога
    }
    for (int x = 20; x < 80; ++x) {
        image.setPixelColor(x, 25, QColor(20, 40, 160));
        image.setPixelColor(x, 26, QColor(20, 40, 160));
    }
    return image;
}

}  // namespace

TEST(saved_signatures, save_load_remove_and_survive_restart) {
    OT_FEATURE("sign-02");
    ot::test::qtApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("signatures"));
    {
        SavedSignatures saved(root);
        EXPECT_FALSE(saved.has(SignatureKind::Signature));
        EXPECT_FALSE(saved.has(SignatureKind::Initials));
        EXPECT_TRUE(saved.load(SignatureKind::Signature).isNull());
        QString error;
        ASSERT_TRUE(saved.save(SignatureKind::Signature, strokes(), &error)) << error.toStdString();
        EXPECT_EQ(saved.path(SignatureKind::Signature), QDir(root).filePath(QStringLiteral("signature.png")));
        EXPECT_EQ(saved.path(SignatureKind::Initials), QDir(root).filePath(QStringLiteral("initials.png")));
        EXPECT_TRUE(QFile::exists(saved.path(SignatureKind::Signature)));
        EXPECT_FALSE(saved.has(SignatureKind::Initials));
        // Пустая картинка не сохраняется.
        EXPECT_FALSE(saved.save(SignatureKind::Initials, QImage(), &error));
    }
    // «Перезапуск»: новый экземпляр читает тот же файл — PNG с альфой.
    SavedSignatures again(root);
    ASSERT_TRUE(again.has(SignatureKind::Signature));
    const QImage loaded = again.load(SignatureKind::Signature);
    ASSERT_EQ(loaded.size(), QSize(120, 40));
    EXPECT_TRUE(loaded.hasAlphaChannel());
    EXPECT_EQ(qAlpha(loaded.pixel(0, 0)), 0);
    EXPECT_EQ(qAlpha(loaded.pixel(15, 20)), 255);
    EXPECT_FALSE(again.png(SignatureKind::Signature).isEmpty());
    EXPECT_EQ(fillsign::inkComponents(loaded), 3);

    ASSERT_TRUE(again.save(SignatureKind::Initials, strokes()));
    EXPECT_TRUE(again.remove(SignatureKind::Signature));
    EXPECT_FALSE(again.has(SignatureKind::Signature));
    EXPECT_FALSE(QFile::exists(again.path(SignatureKind::Signature)));
    EXPECT_TRUE(again.has(SignatureKind::Initials));
    EXPECT_FALSE(again.remove(SignatureKind::Signature));  // уже нет
}

TEST(saved_signatures, default_root_is_signatures_in_data_dir) {
    ot::test::qtApp();
    QTemporaryDir dir;
    const QByteArray before = qgetenv("OTTISK_DATA_DIR");
    qputenv("OTTISK_DATA_DIR", dir.path().toUtf8());
    const SavedSignatures saved;
    EXPECT_EQ(saved.root(), QDir(ot::app::dataDir()).filePath(QStringLiteral("signatures")));
    EXPECT_EQ(fillsign::kindName(SignatureKind::Signature), QStringLiteral("signature"));
    EXPECT_EQ(fillsign::kindName(SignatureKind::Initials), QStringLiteral("initials"));
    if (before.isEmpty()) {
        qunsetenv("OTTISK_DATA_DIR");
    } else {
        qputenv("OTTISK_DATA_DIR", before);
    }
}

TEST(signature_image, white_background_becomes_transparent) {
    OT_FEATURE("sign-01");
    ot::test::qtApp();
    const QImage source = scanned();
    // Чернилами (цвет выбран человеком): фон и шум прозрачные, штрих — красный непрозрачный.
    const QImage inked =
        fillsign::removeBackground(source, fillsign::kDefaultThreshold, false, QColor(200, 0, 0));
    ASSERT_EQ(inked.size(), source.size());
    EXPECT_TRUE(inked.hasAlphaChannel());
    EXPECT_EQ(qAlpha(inked.pixel(5, 10)), 0);     // бумага
    EXPECT_EQ(qAlpha(inked.pixel(7, 3)), 0);      // шум светлее порога
    EXPECT_GT(qAlpha(inked.pixel(50, 25)), 200);  // штрих
    EXPECT_EQ(QColor::fromRgba(inked.pixel(50, 25)).red(), 200);
    EXPECT_EQ(QColor::fromRgba(inked.pixel(50, 25)).blue(), 0);
    // «Сохранить исходный цвет»: штрих синий, как в скане.
    const QImage kept =
        fillsign::removeBackground(source, fillsign::kDefaultThreshold, true, QColor(200, 0, 0));
    EXPECT_GT(QColor::fromRgba(kept.pixel(50, 25)).blue(), 120);
    EXPECT_EQ(qAlpha(kept.pixel(5, 10)), 0);
    // Порог ниже серого шума — шум остаётся.
    const QImage strict = fillsign::removeBackground(source, 240, true, QColor());
    EXPECT_GT(qAlpha(strict.pixel(7, 3)), 0);

    // Обрезка по чернилам с полем 2 px: остаётся штрих 60 × 2 и поля.
    const QImage cropped = fillsign::cropToInk(inked, 2);
    EXPECT_EQ(cropped.size(), QSize(64, 6));
    EXPECT_EQ(fillsign::inkComponents(cropped), 1);
    QImage blank(10, 10, QImage::Format_ARGB32);
    blank.fill(Qt::transparent);
    EXPECT_TRUE(fillsign::cropToInk(blank, 2).isNull());  // чернил нет — пусто

    // Перекраска: альфа прежняя, цвет новый.
    const QImage blue = fillsign::recolor(inked, QColor(0, 0, 255));
    EXPECT_EQ(qAlpha(blue.pixel(50, 25)), qAlpha(inked.pixel(50, 25)));
    EXPECT_EQ(QColor::fromRgba(blue.pixel(50, 25)).blue(), 255);
    EXPECT_EQ(qAlpha(blue.pixel(5, 10)), 0);
    EXPECT_FALSE(fillsign::toPng(blue).isEmpty());
}

TEST(signature_image, ink_components_counts_separate_strokes) {
    ot::test::qtApp();
    EXPECT_EQ(fillsign::inkComponents(strokes()), 3);
    QImage empty(30, 30, QImage::Format_ARGB32);
    empty.fill(Qt::transparent);
    EXPECT_EQ(fillsign::inkComponents(empty), 0);
    EXPECT_FALSE(fillsign::hasInk(empty));
    EXPECT_TRUE(fillsign::hasInk(strokes()));
}
