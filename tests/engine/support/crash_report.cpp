// Падение engine_tests (необработанное исключение SEH: нарушение доступа, точка останова, переполнение стека)
// — код, адрес и стек с именами функций и строками в stderr. Какой файл корпуса уронил процесс, показывает
// OTTISK_CORPUS_TRACE=1, а это — место в коде. __fastfail (abort, /GS) обходит фильтр — там только код
// выхода.
#include <windows.h>

#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace {

LONG WINAPI reportCrash(EXCEPTION_POINTERS* ep) {
    std::fprintf(stderr, "engine_tests: unhandled exception 0x%08lX at %p\n",
                 ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (SymInitialize(process, nullptr, TRUE)) {
        void* frames[64] = {};
        const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        for (USHORT i = 0; i < count; ++i) {
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 511;
            const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
            DWORD64 offset = 0;
            DWORD column = 0;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof line;
            const bool named = SymFromAddr(process, address, &offset, symbol) != FALSE;
            const bool located = SymGetLineFromAddr64(process, address, &column, &line) != FALSE;
            std::fprintf(stderr, "  #%02u %s+0x%llx %s:%lu\n", static_cast<unsigned>(i),
                         named ? symbol->Name : "?", static_cast<unsigned long long>(offset),
                         located ? line.FileName : "", located ? line.LineNumber : 0UL);
        }
    }
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

struct CrashReport {
    CrashReport() { SetUnhandledExceptionFilter(reportCrash); }
};

const CrashReport kCrashReport;

}  // namespace
