#include "support/probe.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "core/mu/error.h"
#include "core/render/renderer.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot::test {

namespace {

std::chrono::seconds renderTimeout() {
#if defined(__SANITIZE_ADDRESS__)
    long long seconds = 45;
#else
    long long seconds = 15;
#endif
    if (const char* v = std::getenv("OTTISK_PROBE_RENDER_TIMEOUT_S"); v != nullptr && *v != '\0') {
        seconds = std::strtoll(v, nullptr, 10);
    }
    return std::chrono::seconds(seconds);
}

// Срок рендера: сторожевой поток ставит cookie.abort, если рендер не уложился (MuPDF проверяет флаг между
// операторами содержимого и узлами display list).
class RenderDeadline {
public:
    explicit RenderDeadline(std::chrono::seconds limit) {
        if (limit.count() > 0) {
            watchdog_ = std::thread([this, limit] {
                std::unique_lock lock(mutex_);
                if (!cv_.wait_for(lock, limit, [this] { return done_; })) {
                    fired_ = true;
                    cookie_.abort = 1;
                }
            });
        }
    }
    ~RenderDeadline() { finish(); }
    RenderDeadline(const RenderDeadline&) = delete;
    RenderDeadline& operator=(const RenderDeadline&) = delete;

    fz_cookie* cookie() noexcept { return &cookie_; }

    // Сработал ли срок (после завершения сторожа).
    bool fired() {
        finish();
        return fired_;
    }

private:
    void finish() {
        {
            const std::lock_guard lock(mutex_);
            done_ = true;
        }
        cv_.notify_one();
        if (watchdog_.joinable()) {
            watchdog_.join();
        }
    }

    fz_cookie cookie_{};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    bool fired_ = false;
    std::thread watchdog_;
};

}  // namespace

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
                        RenderDeadline deadline(renderTimeout());
                        try {
                            renderer.render(req, buf.data(), req.scissor.width() * 3, PixelFormat::Rgb24,
                                            deadline.cookie());
                        } catch (const MuError&) {
                            p.timedOut = deadline.fired();
                            throw;
                        }
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
           fzCode == FZ_ERROR_UNSUPPORTED || fzCode == FZ_ERROR_LIBRARY || fzCode == FZ_ERROR_GENERIC;
}

bool isMemoryFailure(const DocError& e) noexcept {
    return e.code == FZ_ERROR_SYSTEM &&
           (e.message.find("malloc") != std::string::npos || e.message.find("memory") != std::string::npos);
}

bool envFlag(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && !(v[0] == '0' && v[1] == '\0');
}

std::filesystem::path corpusRoot() {
    if (const char* c = std::getenv("OTTISK_CORPUS"); c != nullptr && *c != '\0') {
        return paths::fromUtf8(c);
    }
    // Не %LOCALAPPDATA%: приложение Claude перенаправляет туда записи своих процессов (docs/progress.md).
    return std::filesystem::path(L"C:\\Tools\\ottisk\\corpus");
}

std::filesystem::path corpusFile(const std::string& path, const std::string& rel) {
    if (rel.rfind("repo:", 0) == 0) {
        return paths::fromUtf8(OTTISK_SOURCE_DIR) / paths::fromUtf8(rel.substr(5));
    }
    if (!rel.empty()) {
        std::error_code ec;
        const std::filesystem::path moved = corpusRoot() / paths::fromUtf8(rel);
        if (std::filesystem::exists(moved, ec)) {
            return moved;
        }
    }
    return paths::fromUtf8(path);
}

}  // namespace ot::test
