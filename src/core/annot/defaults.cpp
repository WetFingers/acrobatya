#include "core/annot/defaults.h"

#include <windows.h>

#include <lmcons.h>

#include "core/util/paths.h"

#pragma comment(lib, "advapi32.lib")

namespace ot::annot {

std::string loginName() {
    wchar_t buffer[UNLEN + 1] = {};
    DWORD size = UNLEN + 1;
    if (GetUserNameW(buffer, &size) == 0 || buffer[0] == L'\0') {
        return "User";
    }
    return paths::narrow(std::wstring_view(buffer));
}

std::string authorName(const Identity& identity) {
    if (identity.alwaysLoginName || identity.name.empty()) {
        return loginName();
    }
    return identity.name;
}

std::string defaultSubject(AnnotType type) {
    switch (type) {
        case AnnotType::Text:
            return "Sticky Note";
        case AnnotType::FreeText:
            return "Text Box";
        case AnnotType::Line:
            return "Line";
        case AnnotType::Square:
            return "Rectangle";
        case AnnotType::Circle:
            return "Oval";
        case AnnotType::Polygon:
            return "Polygon";
        case AnnotType::PolyLine:
            return "Polygonal Line";
        case AnnotType::Highlight:
            return "Highlight";
        case AnnotType::Underline:
            return "Underline";
        case AnnotType::Squiggly:
            return "Squiggly";
        case AnnotType::StrikeOut:
            return "Cross-Out";
        case AnnotType::Stamp:
            return "Stamp";
        case AnnotType::Caret:
            return "Inserted Text";
        case AnnotType::Ink:
            return "Pencil";
        case AnnotType::FileAttachment:
            return "File Attachment";
        default:
            return std::string(annotTypeName(type));
    }
}

AnnotProps toolDefaults(AnnotType type) {
    AnnotProps p;
    p.type = type;
    p.subject = defaultSubject(type);
    switch (type) {
        case AnnotType::Text:
            p.color = AnnotColor::rgb(1.0f, 0.82f, 0.0f);
            p.icon = "Comment";
            p.flags = kAnnotPrint | kAnnotNoZoom | kAnnotNoRotate;
            break;
        case AnnotType::Highlight:
            p.color = AnnotColor::rgb(1.0f, 1.0f, 0.0f);
            break;
        case AnnotType::Underline:
            p.color = AnnotColor::rgb(0.0f, 0.6f, 0.0f);
            break;
        case AnnotType::StrikeOut:
            p.color = AnnotColor::rgb(1.0f, 0.0f, 0.0f);
            break;
        case AnnotType::Squiggly:
            p.color = AnnotColor::rgb(0.0f, 0.4f, 1.0f);
            break;
        case AnnotType::Caret:
            p.color = AnnotColor::rgb(0.0f, 0.0f, 1.0f);
            break;
        case AnnotType::Ink:
            p.color = AnnotColor::rgb(0.0f, 0.0f, 1.0f);
            p.borderWidth = 1.0f;
            break;
        case AnnotType::FreeText:
            p.color = AnnotColor{};  // без рамки и фона
            p.borderWidth = 0.0f;
            p.fontSize = 12.0f;
            p.textColor = AnnotColor::gray(0);
            break;
        case AnnotType::Stamp:
            p.color = AnnotColor::rgb(1.0f, 0.0f, 0.0f);
            p.icon = "Draft";
            break;
        case AnnotType::FileAttachment:
            p.color = AnnotColor::rgb(0.0f, 0.0f, 1.0f);
            p.icon = "PushPin";
            p.flags = kAnnotPrint | kAnnotNoZoom | kAnnotNoRotate;
            break;
        default:  // фигуры и линии
            p.color = AnnotColor::rgb(1.0f, 0.0f, 0.0f);
            p.borderWidth = 1.0f;
            break;
    }
    return p;
}

}  // namespace ot::annot
