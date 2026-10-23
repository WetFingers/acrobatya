#include "support/probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <vector>

#include "core/mu/error.h"
#include "core/render/renderer.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot::test {

ProbeResult probeFile(const std::filesystem::path& path, const std::string& password) {
    ProbeResult p;
    const auto t0 = std::chrono::steady_clock::now();
    OpenResult r = Document::open(path, password);
    p.opened = r.ok;
    p.needsPassword = r.needsPassword;
    p.repaired = r.repaired;
    p.error = r.error;
    if (r.ok) {
        try {
            p.pages = r.document->pageCount();
            if (p.pages > 0) {
                const RectF box = r.document->pageBox(0);
                const float side = std::max(box.width(), box.height());
                if (side > 0) {
                    const Renderer renderer(*r.document);
                    RenderRequest req;
                    req.matrix = Matrix::scale(32.0f / side);
                    req.scissor = renderer.deviceBounds(0, req.matrix);
                    if (!req.scissor.empty()) {
                        std::vector<std::uint8_t> buf(static_cast<size_t>(req.scissor.width()) *
                                                      static_cast<size_t>(req.scissor.height()) * 3);
                        renderer.render(req, buf.data(), req.scissor.width() * 3, PixelFormat::Rgb24);
                    }
                }
                p.rendered = true;
            }
        } catch (const MuError& e) {
            p.error = {e.kind(), e.code(), e.what()};
        } catch (const std::exception& e) {
            p.error = {ErrorKind::Other, 0, e.what()};
        }
    }
    r.document.reset();  // закрыть — часть зонда
    p.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return p;
}

bool isBrokenFileCode(int fzCode) noexcept {
    return fzCode == FZ_ERROR_FORMAT || fzCode == FZ_ERROR_SYNTAX || fzCode == FZ_ERROR_LIMIT ||
           fzCode == FZ_ERROR_UNSUPPORTED;
}

std::filesystem::path corpusRoot() {
    if (const char* c = std::getenv("OTTISK_CORPUS"); c != nullptr && *c != '\0') {
        return paths::fromUtf8(c);
    }
    const char* local = std::getenv("LOCALAPPDATA");
    return paths::fromUtf8(local != nullptr ? local : "") / L"ottisk" / L"corpus";
}

}  // namespace ot::test
