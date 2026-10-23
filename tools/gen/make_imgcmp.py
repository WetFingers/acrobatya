# -*- coding: utf-8 -*-
"""make_imgcmp.py — три пары PNG и заранее посчитанные метрики для unit.imgcmp.* (эталон 9.3):
tests/fixtures/imgcmp/{noise,shift,block}-{a,b}.png + expected.json.

Метрики — те же определения, что у pdfium_diff --metrics в эталоне 9.3 и у src/core/render/image_compare.cpp,
но посчитаны здесь независимо (numpy, float64) — это оракул для C++-реализации (самого pdfium_diff на машине
нет): размеры; число отличающихся пикселей (хоть один канал); max delta по каналу; MSE — среднее по пикселям
от (сумма квадратов разностей каналов) / 3; windowed MSE — максимум того же среднего по окнам 8×8 сетки
(краевые окна неполные); SSIM — по серому 0.299R + 0.587G + 0.114B, окна 8×8 с шагом 4 (только целые),
c1 = (0.01·255)², c2 = (0.03·255)², дисперсии по совокупности, среднее по окнам.
Основа — синтетическая «страница» 160 × 120 (текстоподобные штрихи), без внешних файлов.
  noise — ±1…3 в одном канале у 300 случайных пикселей (в пределах порога по умолчанию);
  shift — сдвиг на 1 пиксель вправо (за порогом по wMSE);
  block — чёрный прямоугольник 24 × 16 (за порогом).
Детерминизм: random.Random(seed=1), PNG пишет Pillow без метаданных.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_imgcmp.py [--out <корень>]
"""
import random
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

import _genlib as g  # noqa: E402

W, H = 160, 120


def base_page(rng: random.Random) -> np.ndarray:
    img = np.full((H, W, 3), 255, dtype=np.uint8)
    for row in range(12, H - 10, 14):  # строки «текста»: штрихи разной длины и серого
        x = 10
        while x < W - 14:
            w = rng.randrange(3, 12)
            shade = rng.randrange(0, 90)
            img[row : row + 7, x : x + w] = (shade, shade, shade + rng.randrange(0, 40))
            x += w + rng.randrange(2, 6)
    img[4:8, 10:60] = (200, 30, 30)  # цветной заголовок
    return img


def metrics(a: np.ndarray, b: np.ndarray) -> dict:
    da = a.astype(np.float64)
    db = b.astype(np.float64)
    d = np.abs(da - db)
    per_px_sq = (d**2).sum(axis=2) / 3.0
    res = {
        "same_size": True,
        "width": int(a.shape[1]),
        "height": int(a.shape[0]),
        "differing_pixels": int((d.max(axis=2) > 0).sum()),
        "max_delta": int(d.max()),
        "mse": float(per_px_sq.mean()),
    }
    wmse = 0.0
    for y in range(0, a.shape[0], 8):
        for x in range(0, a.shape[1], 8):
            wmse = max(wmse, float(per_px_sq[y : y + 8, x : x + 8].mean()))
    res["wmse"] = wmse
    ga = 0.299 * da[:, :, 0] + 0.587 * da[:, :, 1] + 0.114 * da[:, :, 2]
    gb = 0.299 * db[:, :, 0] + 0.587 * db[:, :, 1] + 0.114 * db[:, :, 2]
    c1 = (0.01 * 255) ** 2
    c2 = (0.03 * 255) ** 2
    vals = []
    for y in range(0, a.shape[0] - 8 + 1, 4):
        for x in range(0, a.shape[1] - 8 + 1, 4):
            wa = ga[y : y + 8, x : x + 8]
            wb = gb[y : y + 8, x : x + 8]
            ma, mb = wa.mean(), wb.mean()
            va = (wa * wa).mean() - ma * ma
            vb = (wb * wb).mean() - mb * mb
            cov = (wa * wb).mean() - ma * mb
            vals.append(((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2)))
    res["ssim"] = float(np.mean(vals))
    res["within_default"] = res["max_delta"] <= 3 and res["mse"] <= 0.05 and res["wmse"] <= 15 and res["ssim"] >= 0.98
    return res


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "imgcmp"
    dst.mkdir(parents=True, exist_ok=True)
    rng = random.Random(g.SEED)
    a = base_page(rng)

    noise = a.copy()
    for _ in range(300):
        y, x, c = rng.randrange(H), rng.randrange(W), rng.randrange(3)
        delta = rng.choice([-3, -2, -1, 1, 2, 3])
        noise[y, x, c] = np.uint8(min(255, max(0, int(noise[y, x, c]) + delta)))
    shift = np.full_like(a, 255)
    shift[:, 1:] = a[:, :-1]
    block = a.copy()
    block[60:76, 100:124] = 0

    pairs = {"noise": noise, "shift": shift, "block": block}
    expected = {"generator": "tools/gen/make_imgcmp.py", "seed": g.SEED,
                "definition": "эталон 9.3: pdfium_diff --metrics; оракул — numpy float64", "pairs": {}}
    for name, b in pairs.items():
        Image.fromarray(a, "RGB").save(dst / f"{name}-a.png", optimize=False)
        Image.fromarray(b, "RGB").save(dst / f"{name}-b.png", optimize=False)
        expected["pairs"][name] = {"a": f"{name}-a.png", "b": f"{name}-b.png", "metrics": metrics(a, b)}
    g.write_json(dst / "expected.json", expected)
    g.say("imgcmp: 3 пары → %s" % dst)


if __name__ == "__main__":
    main()
