// XFDF как XML (план 5.4; эталон 3.4 «FDF/XFDF»; comment-27): модель core/annot/xfdf.h ↔ текст XML
// (QXmlStreamWriter/Reader). Корень <xfdf xmlns="http://ns.adobe.com/xfdf/" xml:space="preserve">, внутри
// <annots> с элементами аннотаций и <f href="имя.pdf"/>; /RC идёт в <contents-richtext> разметкой XHTML как
// есть (тот же XML, а не строка), вложение — <data MODE="RAW" encoding="base64" length="N">; читаются и
// шестнадцатеричные данные Acrobat (encoding="hex"). Незнакомые элементы пропускаются.
#pragma once

#include <QByteArray>

#include "core/annot/xfdf.h"

namespace ot::xfdf {

QByteArray write(const annot::XfdfDocument& doc);
// Не XFDF (не XML, корень не xfdf) — annot::ExchangeError (Malformed).
annot::XfdfDocument read(const QByteArray& xml);

}  // namespace ot::xfdf
