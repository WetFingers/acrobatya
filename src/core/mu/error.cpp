#include "core/mu/error.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

ErrorKind errorKind(int fzCode) noexcept {
    switch (fzCode) {
        case FZ_ERROR_NONE:
            return ErrorKind::None;
        case FZ_ERROR_FORMAT:
        case FZ_ERROR_SYNTAX:
            return ErrorKind::Damaged;
        case FZ_ERROR_UNSUPPORTED:
            return ErrorKind::Unsupported;
        case FZ_ERROR_LIMIT:
            return ErrorKind::Limit;
        case FZ_ERROR_SYSTEM:
            return ErrorKind::System;
        case FZ_ERROR_ARGUMENT:
            return ErrorKind::Argument;
        case FZ_ERROR_ABORT:
        case FZ_ERROR_TRYLATER:
            return ErrorKind::Aborted;
        default:
            return ErrorKind::Other;
    }
}

const char* MuError::codeName(int fzCode) noexcept {
    switch (fzCode) {
        case FZ_ERROR_NONE:
            return "none";
        case FZ_ERROR_GENERIC:
            return "generic";
        case FZ_ERROR_SYSTEM:
            return "system";
        case FZ_ERROR_LIBRARY:
            return "library";
        case FZ_ERROR_ARGUMENT:
            return "argument";
        case FZ_ERROR_LIMIT:
            return "limit";
        case FZ_ERROR_UNSUPPORTED:
            return "unsupported";
        case FZ_ERROR_FORMAT:
            return "format";
        case FZ_ERROR_SYNTAX:
            return "syntax";
        case FZ_ERROR_TRYLATER:
            return "trylater";
        case FZ_ERROR_ABORT:
            return "abort";
        case FZ_ERROR_REPAIRED:
            return "repaired";
        default:
            return "unknown";
    }
}

}  // namespace ot
