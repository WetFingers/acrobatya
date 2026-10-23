# Сторонние тестовые файлы в `tests/fixtures/` — лицензии и атрибуция

Эталон: `docs/design.md` 9.2 (решение 16). В Git лежат только **свои** сгенерированные файлы (`tools/gen/*`) и выборка
файлов с **пермиссивной** лицензией из внешних корпусов — каталог `tests/fixtures/pick/<корпус>/…`. Всё остальное
(pdf.js, veraPDF-corpus целиком, DSS, OSS-Fuzz, спецификации Adobe, списки доверия, документы Wikimedia Commons)
не копируется в репозиторий и качается по манифесту `tests/corpus/manifest.json` скриптом `tools/fetch_corpus.py`
в `C:\Tools\ottisk\corpus\`.

Правила выборки `pick/`: не больше 200 файлов, каждый ≤ 2 МБ, всего ≤ 60 МБ; файл копируется байт в байт
(sha256 совпадает с файлом корпуса на закреплённой ревизии), меняется только путь: пробелы в именах каталогов и файлов
заменены на `_`. Файлы двоичные — `pick/.gitattributes` (`* binary`) запрещает Git менять концы строк.
Сейчас: **185 файлов, 14.4 МБ** (pdfium_tests — 93, pdfium_resources — 46, safedocs — 21, pdf-differences — 19, bfo — 4, irs — 2).

## Источники

| Каталог `pick/` | Источник и ревизия | Лицензия | Атрибуция |
|---|---|---|---|
| `pdfium_tests/` | <https://pdfium.googlesource.com/pdfium_tests> @ `86219637fec2` | BSD-3-Clause; `third_party/jbig2/` — BSD-2-Clause | © 2014 PDFium Authors; `third_party/jbig2/` — © 2018–2025 the SerenityOS developers |
| `pdfium_resources/` | <https://pdfium.googlesource.com/pdfium> @ `43d55e429cf7`, каталог `testing/resources/` | BSD-3-Clause | © 2014 The PDFium Authors |
| `safedocs/` | <https://github.com/pdf-association/safedocs> @ `68f730ab8c39` | Apache-2.0 (репозиторий); документация — CC BY 4.0 (`NOTICE.txt`) | © 2021 PDF Association, Inc.; DARPA, контракт HR001119C0079 (см. NOTICE ниже) |
| `pdf-differences/` | <https://github.com/pdf-association/pdf-differences> @ `907fe96e52b7` | CC BY 4.0 (PDF-файлы; код репозитория — Apache-2.0) | «PDF files are copyright by the PDF Association» — © PDF Association, <https://creativecommons.org/licenses/by/4.0/>; изменений нет |
| `bfo/` | <https://github.com/bfocom/pdfa-testsuite> @ `c22d80260c7e` (бывший bfosupport/pdfa-testsuite) | CC BY 3.0 | BFO (Big Faceless Organization), «pdfa-testsuite», <https://creativecommons.org/licenses/by/3.0/legalcode>; изменений нет |
| `irs/` | <https://www.irs.gov/pub/irs-pdf/> (снимок 2026-10-23) | общественное достояние | произведения правительства США (17 U.S.C. §105); копии `f1040.pdf`, `fw4.pdf` — запасные для записи `irs` манифеста (поле `mirror`): IRS ежегодно заменяет файлы по тем же адресам |

Не входят в `pick/` и не копируются в Git: `pdfium_tests/third_party/tcpdf/` (LGPL-3.0), `third_party/skia/` (другой
правообладатель), pdf.js `test/pdfs` (лицензия файлов не указана), veraPDF-corpus (CC BY 4.0 — по решению 16 только
по манифесту), DSS (LGPL-2.1, лицензия файлов не оговорена), OSS-Fuzz-корпус MuPDF, `PDF32000_2008.pdf` и
`pdfreference1.7old.pdf` (Adobe), списки доверия Adobe/ЕС, документы Wikimedia Commons (PD-RU-exempt).

## Файлы

| Файл в `tests/fixtures/pick/` | Путь в корпусе | Лицензия | Байт |
|---|---|---|---:|
| `bfo/pdfa2-6-1-5-bfo-t01-pass.pdf` | `pdfa2-6-1-5-bfo-t01-pass.pdf` | CC-BY-3.0 | 324636 |
| `bfo/pdfa2-6-3-2-bfo-t02-fail.pdf` | `pdfa2-6-3-2-bfo-t02-fail.pdf` | CC-BY-3.0 | 326179 |
| `bfo/pdfa2-6-8-bfo-t01-pass.pdf` | `pdfa2-6-8-bfo-t01-pass.pdf` | CC-BY-3.0 | 649579 |
| `bfo/pdfa2-6-9-bfo-t01-fail.pdf` | `pdfa2-6-9-bfo-t01-fail.pdf` | CC-BY-3.0 | 325776 |
| `irs/f1040.pdf` | `f1040.pdf` | public-domain | 220237 |
| `irs/fw4.pdf` | `fw4.pdf` | public-domain | 208845 |
| `pdf-differences/Atomic-Fill+Stroke/FillStrokeOrdering.pdf` | `Atomic-Fill+Stroke/FillStrokeOrdering.pdf` | CC-BY-4.0 | 1937 |
| `pdf-differences/Atomic-Fill+Stroke/SelfIntersecting-Transparency.pdf` | `Atomic-Fill+Stroke/SelfIntersecting-Transparency.pdf` | CC-BY-4.0 | 1857 |
| `pdf-differences/ColorBurn-ColorDodge/ColorBurn.pdf` | `ColorBurn-ColorDodge/ColorBurn.pdf` | CC-BY-4.0 | 3357 |
| `pdf-differences/ColorBurn-ColorDodge/ColorDodge.pdf` | `ColorBurn-ColorDodge/ColorDodge.pdf` | CC-BY-4.0 | 3345 |
| `pdf-differences/Dashing-Degenerate/DegenerateDashing.pdf` | `Dashing-Degenerate/DegenerateDashing.pdf` | CC-BY-4.0 | 4192 |
| `pdf-differences/Dashing-EndBeforeBend/Dashing-EndBeforeBend.pdf` | `Dashing-EndBeforeBend/Dashing-EndBeforeBend.pdf` | CC-BY-4.0 | 2666 |
| `pdf-differences/DefaultColorSpaces/DefaultRGBColourSpaces.pdf` | `DefaultColorSpaces/DefaultRGBColourSpaces.pdf` | CC-BY-4.0 | 5645 |
| `pdf-differences/IndexedColor/IndexedCS_negative_and_high.pdf` | `IndexedColor/IndexedCS_negative_and_high.pdf` | CC-BY-4.0 | 1798 |
| `pdf-differences/LargeMitreLimit/LargeMitreLimit.pdf` | `LargeMitreLimit/LargeMitreLimit.pdf` | CC-BY-4.0 | 3515 |
| `pdf-differences/LineCap-Degenerate/LineCap-Degenerate.pdf` | `LineCap-Degenerate/LineCap-Degenerate.pdf` | CC-BY-4.0 | 1583 |
| `pdf-differences/Negative-DashPhase/Negative-DashPhase.pdf` | `Negative-DashPhase/Negative-DashPhase.pdf` | CC-BY-4.0 | 3702 |
| `pdf-differences/NegativeFontSize/NegativeFontSize.pdf` | `NegativeFontSize/NegativeFontSize.pdf` | CC-BY-4.0 | 3180 |
| `pdf-differences/OverlappingGlyphClipping/OverlappingGlyphClipping.pdf` | `OverlappingGlyphClipping/OverlappingGlyphClipping.pdf` | CC-BY-4.0 | 2794 |
| `pdf-differences/PageLabels-UX/PageLabelsTest.pdf` | `PageLabels-UX/PageLabelsTest.pdf` | CC-BY-4.0 | 8611 |
| `pdf-differences/TextClipModeChanges/TextClippingModeChanges.pdf` | `TextClipModeChanges/TextClippingModeChanges.pdf` | CC-BY-4.0 | 2473 |
| `pdf-differences/Type3WordSpacing/Type3Test.pdf` | `Type3WordSpacing/Type3Test.pdf` | CC-BY-4.0 | 2693 |
| `pdf-differences/UnknownFilter/UnknownFilter-Font.pdf` | `UnknownFilter/UnknownFilter-Font.pdf` | CC-BY-4.0 | 11433 |
| `pdf-differences/UnknownFilter/UnknownFilter-PageContentStream.pdf` | `UnknownFilter/UnknownFilter-PageContentStream.pdf` | CC-BY-4.0 | 11433 |
| `pdf-differences/VerticalText/VerticalText.pdf` | `VerticalText/VerticalText.pdf` | CC-BY-4.0 | 5164 |
| `pdfium_resources/testing/resources/annotation_fileattachment.pdf` | `testing/resources/annotation_fileattachment.pdf` | BSD-3-Clause | 3385 |
| `pdfium_resources/testing/resources/bookmarks.pdf` | `testing/resources/bookmarks.pdf` | BSD-3-Clause | 2239 |
| `pdfium_resources/testing/resources/bug_1302355.pdf` | `testing/resources/bug_1302355.pdf` | BSD-3-Clause | 883 |
| `pdfium_resources/testing/resources/bug_343075986.pdf` | `testing/resources/bug_343075986.pdf` | BSD-3-Clause | 1016 |
| `pdfium_resources/testing/resources/bug_40162073.pdf` | `testing/resources/bug_40162073.pdf` | BSD-3-Clause | 967 |
| `pdfium_resources/testing/resources/bug_642.pdf` | `testing/resources/bug_642.pdf` | BSD-3-Clause | 3270 |
| `pdfium_resources/testing/resources/bug_674771.pdf` | `testing/resources/bug_674771.pdf` | BSD-3-Clause | 995 |
| `pdfium_resources/testing/resources/bug_713197.pdf` | `testing/resources/bug_713197.pdf` | BSD-3-Clause | 387 |
| `pdfium_resources/testing/resources/bug_921.pdf` | `testing/resources/bug_921.pdf` | BSD-3-Clause | 3638 |
| `pdfium_resources/testing/resources/combobox_form.pdf` | `testing/resources/combobox_form.pdf` | BSD-3-Clause | 1738 |
| `pdfium_resources/testing/resources/embedded_attachments.pdf` | `testing/resources/embedded_attachments.pdf` | BSD-3-Clause | 10447 |
| `pdfium_resources/testing/resources/embedded_images.pdf` | `testing/resources/embedded_images.pdf` | BSD-3-Clause | 34279 |
| `pdfium_resources/testing/resources/encrypted.pdf` | `testing/resources/encrypted.pdf` | BSD-3-Clause | 10564 |
| `pdfium_resources/testing/resources/encrypted_hello_world_r2.pdf` | `testing/resources/encrypted_hello_world_r2.pdf` | BSD-3-Clause | 1021 |
| `pdfium_resources/testing/resources/encrypted_hello_world_r3.pdf` | `testing/resources/encrypted_hello_world_r3.pdf` | BSD-3-Clause | 1033 |
| `pdfium_resources/testing/resources/encrypted_hello_world_r5.pdf` | `testing/resources/encrypted_hello_world_r5.pdf` | BSD-3-Clause | 1496 |
| `pdfium_resources/testing/resources/encrypted_hello_world_r6.pdf` | `testing/resources/encrypted_hello_world_r6.pdf` | BSD-3-Clause | 1496 |
| `pdfium_resources/testing/resources/font_weight.pdf` | `testing/resources/font_weight.pdf` | BSD-3-Clause | 1418 |
| `pdfium_resources/testing/resources/hello_world.pdf` | `testing/resources/hello_world.pdf` | BSD-3-Clause | 840 |
| `pdfium_resources/testing/resources/hello_world_2_pages.pdf` | `testing/resources/hello_world_2_pages.pdf` | BSD-3-Clause | 962 |
| `pdfium_resources/testing/resources/image_clipped.pdf` | `testing/resources/image_clipped.pdf` | BSD-3-Clause | 849 |
| `pdfium_resources/testing/resources/jpx_lzw.pdf` | `testing/resources/jpx_lzw.pdf` | BSD-3-Clause | 1152 |
| `pdfium_resources/testing/resources/line_annot.pdf` | `testing/resources/line_annot.pdf` | BSD-3-Clause | 789 |
| `pdfium_resources/testing/resources/listbox_form.pdf` | `testing/resources/listbox_form.pdf` | BSD-3-Clause | 3012 |
| `pdfium_resources/testing/resources/matte.pdf` | `testing/resources/matte.pdf` | BSD-3-Clause | 3195 |
| `pdfium_resources/testing/resources/multiple_form_types.pdf` | `testing/resources/multiple_form_types.pdf` | BSD-3-Clause | 2138 |
| `pdfium_resources/testing/resources/page_labels.pdf` | `testing/resources/page_labels.pdf` | BSD-3-Clause | 10080 |
| `pdfium_resources/testing/resources/pattern_fill.pdf` | `testing/resources/pattern_fill.pdf` | BSD-3-Clause | 754 |
| `pdfium_resources/testing/resources/pattern_fill_uncolored.pdf` | `testing/resources/pattern_fill_uncolored.pdf` | BSD-3-Clause | 802 |
| `pdfium_resources/testing/resources/pattern_stroke_uncolored.pdf` | `testing/resources/pattern_stroke_uncolored.pdf` | BSD-3-Clause | 828 |
| `pdfium_resources/testing/resources/rectangles_multi_pages.pdf` | `testing/resources/rectangles_multi_pages.pdf` | BSD-3-Clause | 1382 |
| `pdfium_resources/testing/resources/redact_annot.pdf` | `testing/resources/redact_annot.pdf` | BSD-3-Clause | 683 |
| `pdfium_resources/testing/resources/rotated_image.pdf` | `testing/resources/rotated_image.pdf` | BSD-3-Clause | 831 |
| `pdfium_resources/testing/resources/rotated_text.pdf` | `testing/resources/rotated_text.pdf` | BSD-3-Clause | 1010 |
| `pdfium_resources/testing/resources/rotated_text_90.pdf` | `testing/resources/rotated_text_90.pdf` | BSD-3-Clause | 810 |
| `pdfium_resources/testing/resources/signature_reason.pdf` | `testing/resources/signature_reason.pdf` | BSD-3-Clause | 1764 |
| `pdfium_resources/testing/resources/simple_xfa.pdf` | `testing/resources/simple_xfa.pdf` | BSD-3-Clause | 5801 |
| `pdfium_resources/testing/resources/tagged_actual_text.pdf` | `testing/resources/tagged_actual_text.pdf` | BSD-3-Clause | 2440 |
| `pdfium_resources/testing/resources/tagged_alt_text.pdf` | `testing/resources/tagged_alt_text.pdf` | BSD-3-Clause | 2433 |
| `pdfium_resources/testing/resources/tagged_nested.pdf` | `testing/resources/tagged_nested.pdf` | BSD-3-Clause | 4830 |
| `pdfium_resources/testing/resources/tagged_table.pdf` | `testing/resources/tagged_table.pdf` | BSD-3-Clause | 3205 |
| `pdfium_resources/testing/resources/text_form.pdf` | `testing/resources/text_form.pdf` | BSD-3-Clause | 927 |
| `pdfium_resources/testing/resources/text_render_mode.pdf` | `testing/resources/text_render_mode.pdf` | BSD-3-Clause | 742 |
| `pdfium_resources/testing/resources/two_signatures.pdf` | `testing/resources/two_signatures.pdf` | BSD-3-Clause | 2781 |
| `pdfium_resources/testing/resources/vertical_text.pdf` | `testing/resources/vertical_text.pdf` | BSD-3-Clause | 3518 |
| `pdfium_resources/testing/resources/weblinks.pdf` | `testing/resources/weblinks.pdf` | BSD-3-Clause | 1172 |
| `pdfium_tests/fx/action/123.pdf` | `fx/action/123.pdf` | BSD-3-Clause | 343753 |
| `pdfium_tests/fx/action/action_reset.pdf` | `fx/action/action_reset.pdf` | BSD-3-Clause | 48855 |
| `pdfium_tests/fx/action/action_run_javascript.pdf` | `fx/action/action_run_javascript.pdf` | BSD-3-Clause | 79035 |
| `pdfium_tests/fx/color/2_color_lab.pdf` | `fx/color/2_color_lab.pdf` | BSD-3-Clause | 1572 |
| `pdfium_tests/fx/color/color_separation.pdf` | `fx/color/color_separation.pdf` | BSD-3-Clause | 876 |
| `pdfium_tests/fx/color/colorspace.pdf` | `fx/color/colorspace.pdf` | BSD-3-Clause | 3998 |
| `pdfium_tests/fx/color/colorspace_test.pdf` | `fx/color/colorspace_test.pdf` | BSD-3-Clause | 5114 |
| `pdfium_tests/fx/font/font_1_embedded_font_en_feature.pdf` | `fx/font/font_1_embedded_font_en_feature.pdf` | BSD-3-Clause | 1577403 |
| `pdfium_tests/fx/font/font_2_embedded_font_en_size14.pdf` | `fx/font/font_2_embedded_font_en_size14.pdf` | BSD-3-Clause | 1507982 |
| `pdfium_tests/fx/font/type3.pdf` | `fx/font/type3.pdf` | BSD-3-Clause | 1201 |
| `pdfium_tests/fx/form/calculate_order.pdf` | `fx/form/calculate_order.pdf` | BSD-3-Clause | 5747 |
| `pdfium_tests/fx/form/check_box.pdf` | `fx/form/check_box.pdf` | BSD-3-Clause | 65619 |
| `pdfium_tests/fx/form/combo_box.pdf` | `fx/form/combo_box.pdf` | BSD-3-Clause | 475217 |
| `pdfium_tests/fx/form/format_date.pdf` | `fx/form/format_date.pdf` | BSD-3-Clause | 9275 |
| `pdfium_tests/fx/form/list_box.pdf` | `fx/form/list_box.pdf` | BSD-3-Clause | 647426 |
| `pdfium_tests/fx/form/push_button.pdf` | `fx/form/push_button.pdf` | BSD-3-Clause | 1780055 |
| `pdfium_tests/fx/form/radio_button.pdf` | `fx/form/radio_button.pdf` | BSD-3-Clause | 73659 |
| `pdfium_tests/fx/form/signature.pdf` | `fx/form/signature.pdf` | BSD-3-Clause | 85818 |
| `pdfium_tests/fx/form/text_field.pdf` | `fx/form/text_field.pdf` | BSD-3-Clause | 456862 |
| `pdfium_tests/fx/form/text_field_multiline_line_spacing.pdf` | `fx/form/text_field_multiline_line_spacing.pdf` | BSD-3-Clause | 3132 |
| `pdfium_tests/fx/image/3_image_imagemask.pdf` | `fx/image/3_image_imagemask.pdf` | BSD-3-Clause | 1276 |
| `pdfium_tests/fx/image/image_8bit_devicergb_dctdecode.pdf` | `fx/image/image_8bit_devicergb_dctdecode.pdf` | BSD-3-Clause | 7337 |
| `pdfium_tests/fx/image/image_foxit.pdf` | `fx/image/image_foxit.pdf` | BSD-3-Clause | 11051 |
| `pdfium_tests/fx/image/mask_array.pdf` | `fx/image/mask_array.pdf` | BSD-3-Clause | 1649 |
| `pdfium_tests/fx/js/calculate_sum_a_b_c.pdf` | `fx/js/calculate_sum_a_b_c.pdf` | BSD-3-Clause | 8288 |
| `pdfium_tests/fx/js/event_change.pdf` | `fx/js/event_change.pdf` | BSD-3-Clause | 3369 |
| `pdfium_tests/fx/layer/ocg.pdf` | `fx/layer/ocg.pdf` | BSD-3-Clause | 812 |
| `pdfium_tests/fx/layer/octest.pdf` | `fx/layer/octest.pdf` | BSD-3-Clause | 5662 |
| `pdfium_tests/fx/matrix/bug_cm_rotate.pdf` | `fx/matrix/bug_cm_rotate.pdf` | BSD-3-Clause | 819 |
| `pdfium_tests/fx/matrix/transformation.pdf` | `fx/matrix/transformation.pdf` | BSD-3-Clause | 1557 |
| `pdfium_tests/fx/mulobj/new/comments/new_arrow.pdf` | `fx/mulobj/new/comments/new_arrow.pdf` | BSD-3-Clause | 1368 |
| `pdfium_tests/fx/mulobj/new/comments/new_attachment.pdf` | `fx/mulobj/new/comments/new_attachment.pdf` | BSD-3-Clause | 6742 |
| `pdfium_tests/fx/mulobj/new/comments/new_distance.pdf` | `fx/mulobj/new/comments/new_distance.pdf` | BSD-3-Clause | 1520 |
| `pdfium_tests/fx/mulobj/new/comments/new_line.pdf` | `fx/mulobj/new/comments/new_line.pdf` | BSD-3-Clause | 1325 |
| `pdfium_tests/fx/mulobj/new/comments/new_oval.pdf` | `fx/mulobj/new/comments/new_oval.pdf` | BSD-3-Clause | 1361 |
| `pdfium_tests/fx/mulobj/new/comments/new_perimeter.pdf` | `fx/mulobj/new/comments/new_perimeter.pdf` | BSD-3-Clause | 2424 |
| `pdfium_tests/fx/mulobj/new/comments/new_polygon.pdf` | `fx/mulobj/new/comments/new_polygon.pdf` | BSD-3-Clause | 1368 |
| `pdfium_tests/fx/mulobj/new/comments/new_polyline.pdf` | `fx/mulobj/new/comments/new_polyline.pdf` | BSD-3-Clause | 1399 |
| `pdfium_tests/fx/mulobj/new/comments/new_rectangle.pdf` | `fx/mulobj/new/comments/new_rectangle.pdf` | BSD-3-Clause | 1524 |
| `pdfium_tests/fx/mulobj/new/stamp/new_stamp1.pdf` | `fx/mulobj/new/stamp/new_stamp1.pdf` | BSD-3-Clause | 4095 |
| `pdfium_tests/fx/mulobj/new/stamp/new_stamp2.pdf` | `fx/mulobj/new/stamp/new_stamp2.pdf` | BSD-3-Clause | 3563 |
| `pdfium_tests/fx/mulobj/new/text_markup/new_textmarkup1.pdf` | `fx/mulobj/new/text_markup/new_textmarkup1.pdf` | BSD-3-Clause | 2337 |
| `pdfium_tests/fx/mulobj/new/text_markup/new_textmarkup2.pdf` | `fx/mulobj/new/text_markup/new_textmarkup2.pdf` | BSD-3-Clause | 2240 |
| `pdfium_tests/fx/mulobj/new/text_markup/new_textmarkup4.pdf` | `fx/mulobj/new/text_markup/new_textmarkup4.pdf` | BSD-3-Clause | 2185 |
| `pdfium_tests/fx/mulobj/new/text_markup/new_textmarkup5.pdf` | `fx/mulobj/new/text_markup/new_textmarkup5.pdf` | BSD-3-Clause | 2729 |
| `pdfium_tests/fx/new/shading_2/shading.pdf` | `fx/new/shading_2/shading.pdf` | BSD-3-Clause | 1960 |
| `pdfium_tests/fx/new/shading_2/shading2.pdf` | `fx/new/shading_2/shading2.pdf` | BSD-3-Clause | 1344 |
| `pdfium_tests/fx/new/shading_2/shading_2_8.pdf` | `fx/new/shading_2/shading_2_8.pdf` | BSD-3-Clause | 1328 |
| `pdfium_tests/fx/other/8.2_outline.pdf` | `fx/other/8.2_outline.pdf` | BSD-3-Clause | 5488 |
| `pdfium_tests/fx/other/form_xobject.pdf` | `fx/other/form_xobject.pdf` | BSD-3-Clause | 735 |
| `pdfium_tests/fx/other/group_xobject.pdf` | `fx/other/group_xobject.pdf` | BSD-3-Clause | 1306 |
| `pdfium_tests/fx/path/11.pdf` | `fx/path/11.pdf` | BSD-3-Clause | 20911 |
| `pdfium_tests/fx/path/catering_1.pdf` | `fx/path/catering_1.pdf` | BSD-3-Clause | 27266 |
| `pdfium_tests/fx/path/clipping_text.pdf` | `fx/path/clipping_text.pdf` | BSD-3-Clause | 848 |
| `pdfium_tests/fx/path/edit_getdash.pdf` | `fx/path/edit_getdash.pdf` | BSD-3-Clause | 1091 |
| `pdfium_tests/fx/path/lines.pdf` | `fx/path/lines.pdf` | BSD-3-Clause | 23400 |
| `pdfium_tests/fx/path/path_1.pdf` | `fx/path/path_1.pdf` | BSD-3-Clause | 10717 |
| `pdfium_tests/fx/path/transparent.pdf` | `fx/path/transparent.pdf` | BSD-3-Clause | 11175 |
| `pdfium_tests/fx/path/transparent1.pdf` | `fx/path/transparent1.pdf` | BSD-3-Clause | 1159 |
| `pdfium_tests/fx/pattern/2_color_tiling.pdf` | `fx/pattern/2_color_tiling.pdf` | BSD-3-Clause | 14808 |
| `pdfium_tests/fx/pattern/2_uncolor_tiling.pdf` | `fx/pattern/2_uncolor_tiling.pdf` | BSD-3-Clause | 2996 |
| `pdfium_tests/fx/shading/2_shading_type1.pdf` | `fx/shading/2_shading_type1.pdf` | BSD-3-Clause | 1234 |
| `pdfium_tests/fx/shading/2_shading_type3.pdf` | `fx/shading/2_shading_type3.pdf` | BSD-3-Clause | 1383 |
| `pdfium_tests/fx/shading/2_shading_type4_h.pdf` | `fx/shading/2_shading_type4_h.pdf` | BSD-3-Clause | 1183 |
| `pdfium_tests/fx/shading/2_shading_type_6_00.pdf` | `fx/shading/2_shading_type_6_00.pdf` | BSD-3-Clause | 1118 |
| `pdfium_tests/fx/text/5.5_simple_font.pdf` | `fx/text/5.5_simple_font.pdf` | BSD-3-Clause | 6626 |
| `pdfium_tests/fx/text/ch_1.pdf` | `fx/text/ch_1.pdf` | BSD-3-Clause | 3724 |
| `pdfium_tests/fx/text/en_contact.pdf` | `fx/text/en_contact.pdf` | BSD-3-Clause | 30733 |
| `pdfium_tests/fx/text/en_diy.pdf` | `fx/text/en_diy.pdf` | BSD-3-Clause | 5259 |
| `pdfium_tests/fx/text/en_foxit.pdf` | `fx/text/en_foxit.pdf` | BSD-3-Clause | 321295 |
| `pdfium_tests/fx/text/en_fqa.pdf` | `fx/text/en_fqa.pdf` | BSD-3-Clause | 987075 |
| `pdfium_tests/fx/text/en_introduce.pdf` | `fx/text/en_introduce.pdf` | BSD-3-Clause | 160444 |
| `pdfium_tests/fx/text/en_system.pdf` | `fx/text/en_system.pdf` | BSD-3-Clause | 639406 |
| `pdfium_tests/fx/text/quick_start_guide.pdf` | `fx/text/quick_start_guide.pdf` | BSD-3-Clause | 1833639 |
| `pdfium_tests/fx/text/zh_page.pdf` | `fx/text/zh_page.pdf` | BSD-3-Clause | 188878 |
| `pdfium_tests/pdfium/annots/annotation_circle.pdf` | `pdfium/annots/annotation_circle.pdf` | BSD-3-Clause | 18082 |
| `pdfium_tests/pdfium/annots/annotation_ellipse_fill.pdf` | `pdfium/annots/annotation_ellipse_fill.pdf` | BSD-3-Clause | 19161 |
| `pdfium_tests/pdfium/annots/annotation_freetext.pdf` | `pdfium/annots/annotation_freetext.pdf` | BSD-3-Clause | 18459 |
| `pdfium_tests/pdfium/annots/annotation_highlight.pdf` | `pdfium/annots/annotation_highlight.pdf` | BSD-3-Clause | 17852 |
| `pdfium_tests/pdfium/annots/annotation_ink.pdf` | `pdfium/annots/annotation_ink.pdf` | BSD-3-Clause | 22981 |
| `pdfium_tests/pdfium/annots/annotation_polygon.pdf` | `pdfium/annots/annotation_polygon.pdf` | BSD-3-Clause | 18638 |
| `pdfium_tests/pdfium/annots/annotation_square_fill_opacity.pdf` | `pdfium/annots/annotation_square_fill_opacity.pdf` | BSD-3-Clause | 18707 |
| `pdfium_tests/pdfium/annots/annotation_squiggly.pdf` | `pdfium/annots/annotation_squiggly.pdf` | BSD-3-Clause | 18772 |
| `pdfium_tests/pdfium/annots/annotation_stamp.pdf` | `pdfium/annots/annotation_stamp.pdf` | BSD-3-Clause | 17787 |
| `pdfium_tests/pdfium/annots/annotation_text.pdf` | `pdfium/annots/annotation_text.pdf` | BSD-3-Clause | 18024 |
| `pdfium_tests/pdfium/bug_481_coons_patch.pdf` | `pdfium/bug_481_coons_patch.pdf` | BSD-3-Clause | 690 |
| `pdfium_tests/pdfium/bug_481_tensor_product.pdf` | `pdfium/bug_481_tensor_product.pdf` | BSD-3-Clause | 690 |
| `pdfium_tests/third_party/jbig2/bitmap-halftone.pdf` | `third_party/jbig2/bitmap-halftone.pdf` | BSD-2-Clause | 1287 |
| `pdfium_tests/third_party/jbig2/bitmap-mmr.pdf` | `third_party/jbig2/bitmap-mmr.pdf` | BSD-2-Clause | 1122 |
| `pdfium_tests/third_party/jbig2/bitmap-refine.pdf` | `third_party/jbig2/bitmap-refine.pdf` | BSD-2-Clause | 1128 |
| `pdfium_tests/third_party/jbig2/bitmap-symbol-manysyms.pdf` | `third_party/jbig2/bitmap-symbol-manysyms.pdf` | BSD-2-Clause | 384975 |
| `pdfium_tests/xfa_specific/fx/test_case_0001/Choose.pdf` | `xfa_specific/fx/test_case_0001/Choose.pdf` | BSD-3-Clause | 80422 |
| `pdfium_tests/xfa_specific/fx/test_case_0030/Test_TextField.pdf` | `xfa_specific/fx/test_case_0030/Test_TextField.pdf` | BSD-3-Clause | 80109 |
| `safedocs/CompactedSyntax/CompactedPDFSyntaxMatrix.pdf` | `CompactedSyntax/CompactedPDFSyntaxMatrix.pdf` | Apache-2.0 | 324169 |
| `safedocs/CompactedSyntax/CompactedPDFSyntaxTest.pdf` | `CompactedSyntax/CompactedPDFSyntaxTest.pdf` | Apache-2.0 | 4011 |
| `safedocs/Dialects/Dialect-ContentStreams.pdf` | `Dialects/Dialect-ContentStreams.pdf` | Apache-2.0 | 3179 |
| `safedocs/Dialects/Dialect-ContentStreamsViaResourceNames.pdf` | `Dialects/Dialect-ContentStreamsViaResourceNames.pdf` | Apache-2.0 | 3275 |
| `safedocs/Dialects/Dialect-ContentStreamsWithIndirectRefs.pdf` | `Dialects/Dialect-ContentStreamsWithIndirectRefs.pdf` | Apache-2.0 | 3222 |
| `safedocs/Dialects/Dialect-DictIsStream.pdf` | `Dialects/Dialect-DictIsStream.pdf` | Apache-2.0 | 788 |
| `safedocs/Dialects/Dialect-StreamIsDict.pdf` | `Dialects/Dialect-StreamIsDict.pdf` | Apache-2.0 | 755 |
| `safedocs/Inline_Image_Abbreviations/InlineAbbreviations.pdf` | `Inline Image Abbreviations/InlineAbbreviations.pdf` | Apache-2.0 | 15125 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/ContentStreamCycleType3insideType3.pdf` | `Miscellaneous Targeted Test PDFs/ContentStreamCycleType3insideType3.pdf` | Apache-2.0 | 2440 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/ContentStreamNoCycleType3insideType3.pdf` | `Miscellaneous Targeted Test PDFs/ContentStreamNoCycleType3insideType3.pdf` | Apache-2.0 | 2440 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/Dual-startxref.pdf` | `Miscellaneous Targeted Test PDFs/Dual-startxref.pdf` | Apache-2.0 | 1637 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/FontinsideType3insideType3.pdf` | `Miscellaneous Targeted Test PDFs/FontinsideType3insideType3.pdf` | Apache-2.0 | 1959 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/LinkAnnot-appearances.pdf` | `Miscellaneous Targeted Test PDFs/LinkAnnot-appearances.pdf` | Apache-2.0 | 1702 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/PDF-NoPageContents.pdf` | `Miscellaneous Targeted Test PDFs/PDF-NoPageContents.pdf` | Apache-2.0 | 818 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/PatternTextInsideText.pdf` | `Miscellaneous Targeted Test PDFs/PatternTextInsideText.pdf` | Apache-2.0 | 1275 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/list-of-uri-actions-with-base.pdf` | `Miscellaneous Targeted Test PDFs/list-of-uri-actions-with-base.pdf` | Apache-2.0 | 3314 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/list-of-uri-actions.pdf` | `Miscellaneous Targeted Test PDFs/list-of-uri-actions.pdf` | Apache-2.0 | 2258 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/uri-ismap-test.pdf` | `Miscellaneous Targeted Test PDFs/uri-ismap-test.pdf` | Apache-2.0 | 137201 |
| `safedocs/Miscellaneous_Targeted_Test_PDFs/utf16LE-test.pdf` | `Miscellaneous Targeted Test PDFs/utf16LE-test.pdf` | Apache-2.0 | 14417 |
| `safedocs/Unicode_passwords/baseline/unicode-test.pdf` | `Unicode passwords/baseline/unicode-test.pdf` | Apache-2.0 | 4421 |
| `safedocs/Unicode_passwords/corrigendum5/unicode-corrigendum5-fixed.pdf` | `Unicode passwords/corrigendum5/unicode-corrigendum5-fixed.pdf` | Apache-2.0 | 5273 |

## Тексты лицензий и уведомлений

### PDFium (BSD-3-Clause) — `pdfium_tests/`

```text
Copyright 2014 PDFium Authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
   * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### PDFium `testing/resources` (BSD-3-Clause) — `pdfium_resources/`

```text
Copyright 2014 The PDFium Authors

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
   * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


                                 Apache License
                           Version 2.0, January 2004
                        https://www.apache.org/licenses/

   TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION

   1. Definitions.

      "License" shall mean the terms and conditions for use, reproduction,
      and distribution as defined by Sections 1 through 9 of this document.

      "Licensor" shall mean the copyright owner or entity authorized by
      the copyright owner that is granting the License.

      "Legal Entity" shall mean the union of the acting entity and all
      other entities that control, are controlled by, or are under common
      control with that entity. For the purposes of this definition,
      "control" means (i) the power, direct or indirect, to cause the
      direction or management of such entity, whether by contract or
      otherwise, or (ii) ownership of fifty percent (50%) or more of the
      outstanding shares, or (iii) beneficial ownership of such entity.

      "You" (or "Your") shall mean an individual or Legal Entity
      exercising permissions granted by this License.

      "Source" form shall mean the preferred form for making modifications,
      including but not limited to software source code, documentation
      source, and configuration files.

      "Object" form shall mean any form resulting from mechanical
      transformation or translation of a Source form, including but
      not limited to compiled object code, generated documentation,
      and conversions to other media types.

      "Work" shall mean the work of authorship, whether in Source or
      Object form, made available under the License, as indicated by a
      copyright notice that is included in or attached to the work
      (an example is provided in the Appendix below).

      "Derivative Works" shall mean any work, whether in Source or Object
      form, that is based on (or derived from) the Work and for which the
      editorial revisions, annotations, elaborations, or other modifications
      represent, as a whole, an original work of authorship. For the purposes
      of this License, Derivative Works shall not include works that remain
      separable from, or merely link (or bind by name) to the interfaces of,
      the Work and Derivative Works thereof.

      "Contribution" shall mean any work of authorship, including
      the original version of the Work and any modifications or additions
      to that Work or Derivative Works thereof, that is intentionally
      submitted to Licensor for inclusion in the Work by the copyright owner
      or by an individual or Legal Entity authorized to submit on behalf of
      the copyright owner. For the purposes of this definition, "submitted"
      means any form of electronic, verbal, or written communication sent
      to the Licensor or its representatives, including but not limited to
      communication on electronic mailing lists, source code control systems,
      and issue tracking systems that are managed by, or on behalf of, the
      Licensor for the purpose of discussing and improving the Work, but
      excluding communication that is conspicuously marked or otherwise
      designated in writing by the copyright owner as "Not a Contribution."

      "Contributor" shall mean Licensor and any individual or Legal Entity
      on behalf of whom a Contribution has been received by Licensor and
      subsequently incorporated within the Work.

   2. Grant of Copyright License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      copyright license to reproduce, prepare Derivative Works of,
      publicly display, publicly perform, sublicense, and distribute the
      Work and such Derivative Works in Source or Object form.

   3. Grant of Patent License. Subject to the terms and conditions of
      this License, each Contributor hereby grants to You a perpetual,
      worldwide, non-exclusive, no-charge, royalty-free, irrevocable
      (except as stated in this section) patent license to make, have made,
      use, offer to sell, sell, import, and otherwise transfer the Work,
      where such license applies only to those patent claims licensable
      by such Contributor that are necessarily infringed by their
      Contribution(s) alone or by combination of their Contribution(s)
      with the Work to which such Contribution(s) was submitted. If You
      institute patent litigation against any entity (including a
      cross-claim or counterclaim in a lawsuit) alleging that the Work
      or a Contribution incorporated within the Work constitutes direct
      or contributory patent infringement, then any patent licenses
      granted to You under this License for that Work shall terminate
      as of the date such litigation is filed.

   4. Redistribution. You may reproduce and distribute copies of the
      Work or Derivative Works thereof in any medium, with or without
      modifications, and in Source or Object form, provided that You
      meet the following conditions:

      (a) You must give any other recipients of the Work or
          Derivative Works a copy of this License; and

      (b) You must cause any modified files to carry prominent notices
          stating that You changed the files; and

      (c) You must retain, in the Source form of any Derivative Works
          that You distribute, all copyright, patent, trademark, and
          attribution notices from the Source form of the Work,
          excluding those notices that do not pertain to any part of
          the Derivative Works; and

      (d) If the Work includes a "NOTICE" text file as part of its
          distribution, then any Derivative Works that You distribute must
          include a readable copy of the attribution notices contained
          within such NOTICE file, excluding those notices that do not
          pertain to any part of the Derivative Works, in at least one
          of the following places: within a NOTICE text file distributed
          as part of the Derivative Works; within the Source form or
          documentation, if provided along with the Derivative Works; or,
          within a display generated by the Derivative Works, if and
          wherever such third-party notices normally appear. The contents
          of the NOTICE file are for informational purposes only and
          do not modify the License. You may add Your own attribution
          notices within Derivative Works that You distribute, alongside
          or as an addendum to the NOTICE text from the Work, provided
          that such additional attribution notices cannot be construed
          as modifying the License.

      You may add Your own copyright statement to Your modifications and
      may provide additional or different license terms and conditions
      for use, reproduction, or distribution of Your modifications, or
      for any such Derivative Works as a whole, provided Your use,
      reproduction, and distribution of the Work otherwise complies with
      the conditions stated in this License.

   5. Submission of Contributions. Unless You explicitly state otherwise,
      any Contribution intentionally submitted for inclusion in the Work
      by You to the Licensor shall be under the terms and conditions of
      this License, without any additional terms or conditions.
      Notwithstanding the above, nothing herein shall supersede or modify
      the terms of any separate license agreement you may have executed
      with Licensor regarding such Contributions.

   6. Trademarks. This License does not grant permission to use the trade
      names, trademarks, service marks, or product names of the Licensor,
      except as required for reasonable and customary use in describing the
      origin of the Work and reproducing the content of the NOTICE file.

   7. Disclaimer of Warranty. Unless required by applicable law or
      agreed to in writing, Licensor provides the Work (and each
      Contributor provides its Contributions) on an "AS IS" BASIS,
      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
      implied, including, without limitation, any warranties or conditions
      of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
      PARTICULAR PURPOSE. You are solely responsible for determining the
      appropriateness of using or redistributing the Work and assume any
      risks associated with Your exercise of permissions under this License.

   8. Limitation of Liability. In no event and under no legal theory,
      whether in tort (including negligence), contract, or otherwise,
      unless required by applicable law (such as deliberate and grossly
      negligent acts) or agreed to in writing, shall any Contributor be
      liable to You for damages, including any direct, indirect, special,
      incidental, or consequential damages of any character arising as a
      result of this License or out of the use or inability to use the
      Work (including but not limited to damages for loss of goodwill,
      work stoppage, computer failure or malfunction, or any and all
      other commercial damages or losses), even if such Contributor
      has been advised of the possibility of such damages.

   9. Accepting Warranty or Additional Liability. While redistributing
      the Work or Derivative Works thereof, You may choose to offer,
      and charge a fee for, acceptance of support, warranty, indemnity,
      or other liability obligations and/or rights consistent with this
      License. However, in accepting such obligations, You may act only
      on Your own behalf and on Your sole responsibility, not on behalf
      of any other Contributor, and only if You agree to indemnify,
      defend, and hold each Contributor harmless for any liability
      incurred by, or claims asserted against, such Contributor by reason
      of your accepting any such warranty or additional liability.

   END OF TERMS AND CONDITIONS

   APPENDIX: How to apply the Apache License to your work.

      To apply the Apache License to your work, attach the following
      boilerplate notice, with the fields enclosed by brackets "[]"
      replaced with your own identifying information. (Don't include
      the brackets!)  The text should be enclosed in the appropriate
      comment syntax for the file format. We also recommend that a
      file or class name and description of purpose be included on the
      same "printed page" as the copyright notice for easier
      identification within third-party archives.

   Copyright [yyyy] [name of copyright owner]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       https://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
```

### SerenityOS JBIG2 tests (BSD-2-Clause) — `pdfium_tests/third_party/jbig2/`

```text
BSD 2-Clause License

Copyright (c) 2018-2025, the SerenityOS developers.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### safedocs — NOTICE.txt (Apache-2.0, <https://www.apache.org/licenses/LICENSE-2.0>)

```text
Copyright 2021 PDF Association, Inc. https://www.pdfa.org

This material is based upon work supported by the Defense Advanced
Research Projects Agency (DARPA) under Contract No. HR001119C0079.
Any opinions, findings and conclusions or recommendations expressed
in this material are those of the author(s) and do not necessarily
reflect the views of the Defense Advanced Research Projects Agency
(DARPA). Approved for public release.

Software:
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Other Documentation:
This work is licensed under the Creative Commons Attribution 4.0 International
License. To view a copy of this license, visit http://creativecommons.org/licenses/by/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
```

### pdf-differences — CC BY 4.0

PDF files © PDF Association, Inc.; лицензия <https://creativecommons.org/licenses/by/4.0/>. Файлы не изменены.

### BFO pdfa-testsuite — CC BY 3.0

© BFO; лицензия <https://creativecommons.org/licenses/by/3.0/legalcode>. Файлы не изменены.
