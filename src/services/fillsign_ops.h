// Fill & Sign через DocumentSession (план 5.3, п. 2–3): геометрия бланка страницы для умного размещения
// (текстовый слой с векторами — задача потока документа) и несколько аннотаций одним шагом истории
// (гребёнка). Подпись и инициалы — штамп-картинка (annot_ops.h createImageStamp; шаг «Add signature»).
#pragma once

#include <QFuture>
#include <QString>

#include <vector>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"
#include "services/document_session.h"

namespace ot::fillsign {

inline constexpr const char* kAddTextStep = "Add text";
inline constexpr const char* kAddMarkStep = "Add mark";
inline constexpr const char* kAddSignatureStep = "Add signature";
inline constexpr const char* kAddInitialsStep = "Add initials";
inline constexpr const char* kSignatureColorStep = "Set signature color";
// /Name штампов подписи и инициалов Fill & Sign (не криптографическая подпись — в панели «Подписи» её нет).
inline constexpr const char* kSignatureStampName = "FillSignSignature";
inline constexpr const char* kInitialsStampName = "FillSignInitials";

// Геометрия страницы page (core/annot/fillsign.h geometryOf по TextPage с collectVectors).
QFuture<PageGeometry> geometry(DocumentSession& session, int page,
                               TaskPriority priority = TaskPriority::High);
// Аннотации items на странице page одним шагом step.
QFuture<std::vector<annot::AnnotRef>> createAll(DocumentSession& session, int page,
                                                std::vector<annot::AnnotProps> items,
                                                QString step = QString::fromLatin1(kAddTextStep));

// Подпись или инициалы штампом-картинкой png (PNG с альфой) в рамке props.rect: шаг «Add signature» /
// «Add initials».
QFuture<annot::AnnotRef> createSignature(DocumentSession& session, int page, std::string png,
                                         annot::AnnotProps props, bool initials);

}  // namespace ot::fillsign
