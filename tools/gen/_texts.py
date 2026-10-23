# -*- coding: utf-8 -*-
"""Тексты для фикстур «Оттиска»: свои, написаны для проекта (свободны от чужих прав).

Что: абзацы на русском, английском и немецком для страниц, сканов, демо и большого файла.
Детерминизм: только константы; «случайный» текст — через random.Random(seed).
"""
import random

RU = [
    "Оттиск открывает документы, показывает страницы и помогает работать с ними без лишних окон.",
    "Каждая страница рисуется заново при изменении масштаба, поэтому мелкий шрифт остаётся чётким.",
    "Если файл повреждён, программа пытается восстановить таблицу объектов и сообщает об этом в строке состояния.",
    "Комментарии хранятся прямо в документе: заметки, выделения, подчёркивания и рисунки от руки.",
    "Форму можно заполнить, сохранить и отправить; сумма в итоговом поле пересчитывается сама.",
    "Подпись подтверждает, что после подписания документ не меняли, а сертификат принадлежит подписанту.",
    "Распознавание текста превращает снимок страницы в документ, по которому работает поиск.",
    "Защищённый файл открывается только с паролем, а права владельца ограничивают печать и правку.",
    "Большие документы листаются плавно: страницы загружаются по мере прокрутки и не держат память.",
    "Закладки ведут к главам, ссылки внутри текста переносят к нужной странице одним щелчком.",
    "Ёжик в тумане искал свою лошадь, а над рекой медленно поднималась луна.",
    "Чёрный кот съел шесть жирных щук и запил их холодной водой из ведра.",
]

EN = [
    "Ottisk opens documents, shows their pages and lets you work with them without extra windows.",
    "Every page is redrawn when the zoom level changes, so small print stays sharp and readable.",
    "When a file is damaged, the program tries to rebuild the object table and reports it quietly.",
    "Comments live inside the document: notes, highlights, underlines and freehand drawings.",
    "A form can be filled in, saved and sent; the total field recalculates on its own.",
    "A signature proves that the document has not changed since signing and who signed it.",
    "Text recognition turns a picture of a page into a document that search can work with.",
    "The quick brown fox jumps over the lazy dog while the river flows past the old mill.",
]

DE = [
    "Ottisk öffnet Dokumente, zeigt ihre Seiten und erlaubt die Arbeit ohne zusätzliche Fenster.",
    "Jede Seite wird beim Ändern der Vergrößerung neu gezeichnet, damit kleine Schrift scharf bleibt.",
    "Wenn eine Datei beschädigt ist, versucht das Programm die Objekttabelle wiederherzustellen.",
    "Kommentare werden direkt im Dokument gespeichert: Notizen, Markierungen und Zeichnungen.",
    "Ein Formular lässt sich ausfüllen und speichern; die Summe im Feld wird selbst berechnet.",
    "Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich bei frischem Wind.",
    "Die Straße führt über die Brücke zum Bahnhof, wo der Zug um acht Uhr abfährt.",
    "Größere Dokumente lassen sich flüssig blättern, weil die Seiten erst beim Lesen geladen werden.",
]

RU_WORDS = (
    "документ страница файл текст строка абзац шрифт подпись форма поле значение кнопка окно "
    "печать масштаб поиск закладка ссылка ремонт объект таблица поток картинка слой цвет линия "
    "рамка заметка выделение пароль владелец пользователь ключ сертификат время дата число "
    "сумма итог глава раздел пункт список порядок версия запись проверка результат образец"
).split()

EN_WORDS = (
    "document page file text line paragraph font signature form field value button window print "
    "zoom search bookmark link repair object table stream image layer color border note password"
).split()


def sentences(rng: random.Random, n: int, words=RU_WORDS) -> str:
    out = []
    for _ in range(n):
        k = rng.randint(6, 14)
        ws = [rng.choice(words) for _ in range(k)]
        out.append(" ".join(ws).capitalize() + ".")
    return " ".join(out)
