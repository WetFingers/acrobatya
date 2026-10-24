#include "services/system_fonts_dwrite.h"

#include <windows.h>

#include <dwrite_3.h>
#include <wrl/client.h>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "core/util/paths.h"
#include "services/settings.h"

#pragma comment(lib, "dwrite.lib")

namespace ot {

using Microsoft::WRL::ComPtr;

namespace dwrite_detail {

std::string localized(IDWriteLocalizedStrings* strings) {
    if (strings == nullptr || strings->GetCount() == 0) {
        return {};
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(strings->FindLocaleName(L"en-us", &index, &exists)) || !exists) {
        index = 0;
    }
    UINT32 len = 0;
    if (FAILED(strings->GetStringLength(index, &len))) {
        return {};
    }
    std::wstring buf(len + 1, L'\0');
    if (FAILED(strings->GetString(index, buf.data(), len + 1))) {
        return {};
    }
    buf.resize(len);
    return paths::narrow(buf);
}

std::string familyName(IDWriteFontFamily* family) {
    ComPtr<IDWriteLocalizedStrings> names;
    if (family == nullptr || FAILED(family->GetFamilyNames(&names))) {
        return {};
    }
    return localized(names.Get());
}

std::string informational(IDWriteFont* font, DWRITE_INFORMATIONAL_STRING_ID id) {
    ComPtr<IDWriteLocalizedStrings> strings;
    BOOL exists = FALSE;
    if (FAILED(font->GetInformationalStrings(id, &strings, &exists)) || !exists) {
        return {};
    }
    return localized(strings.Get());
}

// Путь к файлу грани (только локальные файлы) и её индекс в коллекции.
bool faceFile(IDWriteFontFace* face, std::filesystem::path& path, int& index) {
    UINT32 count = 0;
    if (FAILED(face->GetFiles(&count, nullptr)) || count == 0) {
        return false;
    }
    std::vector<IDWriteFontFile*> files(count, nullptr);
    if (FAILED(face->GetFiles(&count, files.data()))) {
        return false;
    }
    bool ok = false;
    const void* key = nullptr;
    UINT32 keySize = 0;
    ComPtr<IDWriteFontFileLoader> loader;
    ComPtr<IDWriteLocalFontFileLoader> local;
    UINT32 len = 0;
    if (SUCCEEDED(files[0]->GetReferenceKey(&key, &keySize)) && SUCCEEDED(files[0]->GetLoader(&loader)) &&
        SUCCEEDED(loader.As(&local)) && SUCCEEDED(local->GetFilePathLengthFromKey(key, keySize, &len))) {
        std::wstring buf(len + 1, L'\0');
        if (SUCCEEDED(local->GetFilePathFromKey(key, keySize, buf.data(), len + 1))) {
            buf.resize(len);
            path = std::filesystem::path(buf);
            index = static_cast<int>(face->GetIndex());
            ok = true;
        }
    }
    for (IDWriteFontFile* f : files) {
        if (f != nullptr) {
            f->Release();
        }
    }
    return ok;
}

std::optional<edit::SystemFace> describe(IDWriteFont* font, std::string family) {
    ComPtr<IDWriteFontFace> face;
    if (font == nullptr || FAILED(font->CreateFontFace(&face))) {
        return std::nullopt;
    }
    edit::SystemFace out;
    if (!faceFile(face.Get(), out.file, out.index)) {
        return std::nullopt;
    }
    if (family.empty()) {
        ComPtr<IDWriteFontFamily> fam;
        if (SUCCEEDED(font->GetFontFamily(&fam))) {
            family = familyName(fam.Get());
        }
    }
    out.family = std::move(family);
    out.postscript = informational(font, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME);
    out.weight = static_cast<int>(font->GetWeight());
    out.italic = font->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
    const DWRITE_FONT_SIMULATIONS sim = font->GetSimulations();
    out.simulatedBold = (sim & DWRITE_FONT_SIMULATIONS_BOLD) != 0;
    out.simulatedItalic = (sim & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0;
    return out;
}

}  // namespace dwrite_detail

struct DWriteSystemFonts::Impl {
    ComPtr<IDWriteFactory3> factory;
    ComPtr<IDWriteFontCollection> collection;
    ComPtr<IDWriteFontSet> set;
};

DWriteSystemFonts::DWriteSystemFonts() : d_(std::make_unique<Impl>()) {
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                     reinterpret_cast<IUnknown**>(d_->factory.GetAddressOf()));
    if (SUCCEEDED(hr)) {
        hr = d_->factory->GetSystemFontCollection(&d_->collection, FALSE);
    }
    if (SUCCEEDED(hr)) {
        hr = d_->factory->GetSystemFontSet(&d_->set);
    }
    if (FAILED(hr)) {
        spdlog::warn("system fonts: DirectWrite unavailable (hr 0x{:08x})", static_cast<unsigned>(hr));
        d_->collection.Reset();
        d_->set.Reset();
    }
}

DWriteSystemFonts::~DWriteSystemFonts() = default;

bool DWriteSystemFonts::available() const noexcept { return d_->collection != nullptr; }

std::optional<edit::SystemFace> DWriteSystemFonts::face(std::string_view family, int weight, bool italic) {
    if (family.empty() || !available()) {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::wstring wide = paths::widen(family);
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(d_->collection->FindFamilyName(wide.c_str(), &index, &exists)) || !exists) {
        return std::nullopt;
    }
    ComPtr<IDWriteFontFamily> fam;
    ComPtr<IDWriteFont> font;
    if (FAILED(d_->collection->GetFontFamily(index, &fam)) ||
        FAILED(fam->GetFirstMatchingFont(
            static_cast<DWRITE_FONT_WEIGHT>(std::clamp(weight, 1, 999)), DWRITE_FONT_STRETCH_NORMAL,
            italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, &font))) {
        return std::nullopt;
    }
    return dwrite_detail::describe(font.Get(), dwrite_detail::familyName(fam.Get()));
}

std::optional<edit::SystemFace> DWriteSystemFonts::byName(std::string_view name) {
    if (name.empty() || !available() || d_->set == nullptr) {
        return std::nullopt;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::wstring wide = paths::widen(name);
    for (DWRITE_FONT_PROPERTY_ID id :
         {DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME, DWRITE_FONT_PROPERTY_ID_FULL_NAME}) {
        const DWRITE_FONT_PROPERTY prop{id, wide.c_str(), nullptr};
        ComPtr<IDWriteFontSet> matched;
        if (FAILED(d_->set->GetMatchingFonts(&prop, 1, &matched)) || matched->GetFontCount() == 0) {
            continue;
        }
        ComPtr<IDWriteFontFaceReference> ref;
        ComPtr<IDWriteFontFace3> face;
        ComPtr<IDWriteFont> font;
        if (SUCCEEDED(matched->GetFontFaceReference(0, &ref)) && SUCCEEDED(ref->CreateFontFace(&face)) &&
            SUCCEEDED(d_->collection->GetFontFromFontFace(face.Get(), &font))) {
            return dwrite_detail::describe(font.Get(), {});
        }
    }
    return std::nullopt;
}

std::vector<std::string> DWriteSystemFonts::families() {
    std::vector<std::string> out;
    if (!available()) {
        return out;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    const UINT32 n = d_->collection->GetFontFamilyCount();
    out.reserve(n);
    for (UINT32 i = 0; i < n; ++i) {
        ComPtr<IDWriteFontFamily> fam;
        if (SUCCEEDED(d_->collection->GetFontFamily(i, &fam))) {
            if (std::string name = dwrite_detail::familyName(fam.Get()); !name.empty()) {
                out.push_back(std::move(name));
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

edit::SystemFonts& systemFonts() {
    static DWriteSystemFonts fonts;
    return fonts;
}

edit::FallbackFonts editFallbackFonts(const Settings& settings) {
    edit::FallbackFonts f;
    f.family = settings.get(settings::kEditFallbackFont).trimmed().toStdString();
    return f;
}

}  // namespace ot
