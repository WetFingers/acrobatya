// Падение engine_tests (необработанное исключение SEH: переполнение стека, точка останова, исключение вне
// тела теста — нарушения доступа в теле теста ловит сам gtest) — код, адрес и стек с именами функций и
// строками в stderr. Какой файл корпуса уронил процесс, показывает OTTISK_CORPUS_TRACE=1, а это — место в
// коде. Фильтр только копирует контекст и ждёт отдельный поток со своим стеком: при переполнении стека
// упавшего потока места на SymInitialize нет (главному потоку ещё и SetThreadStackGuarantee). __fastfail
// (abort, /GS) обходит фильтр.
#include <windows.h>

#include <dbghelp.h>

#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

namespace {

struct CrashJob {
    CONTEXT context;
    HANDLE thread;  // упавший поток (настоящий описатель)
};

CrashJob g_job;  // не на стеке: он может быть исчерпан

DWORD WINAPI walkStack(void*) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE)) {
        return 1;
    }
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = g_job.context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = g_job.context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = g_job.context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    for (unsigned i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, g_job.thread, &frame, &g_job.context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 offset = 0;
        DWORD column = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof line;
        const bool named = SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol) != FALSE;
        const bool located = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &column, &line) != FALSE;
        std::fprintf(stderr, "  #%02u %s+0x%llx %s:%lu\n", i, named ? symbol->Name : "?",
                     static_cast<unsigned long long>(offset), located ? line.FileName : "",
                     located ? line.LineNumber : 0UL);
    }
    std::fflush(stderr);
    return 0;
}

LONG WINAPI reportCrash(EXCEPTION_POINTERS* ep) {
    std::fprintf(stderr, "engine_tests: unhandled exception 0x%08lX at %p\n",
                 ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    std::fflush(stderr);
    g_job.context = *ep->ContextRecord;
    g_job.thread = nullptr;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_job.thread, 0, FALSE,
                        DUPLICATE_SAME_ACCESS)) {
        if (HANDLE walker = CreateThread(nullptr, 1u << 20, walkStack, nullptr, 0, nullptr)) {
            WaitForSingleObject(walker, 60000);
            CloseHandle(walker);
        }
        CloseHandle(g_job.thread);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

struct CrashReport {
    CrashReport() {
        ULONG guarantee = 64 * 1024;  // место для фильтра после переполнения стека главного потока
        SetThreadStackGuarantee(&guarantee);
        SetUnhandledExceptionFilter(reportCrash);
    }
};

const CrashReport kCrashReport;

}  // namespace
