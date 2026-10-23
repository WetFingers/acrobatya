// Предел памяти процесса engine_tests (Windows Job Object, JOB_OBJECT_LIMIT_PROCESS_MEMORY).
// Файлы-фаззы корпуса заставляют MuPDF выделять гигабайты; 23.10.2026 десять параллельных шардов `fuzz`
// исчерпали память системы и Windows упала (bugcheck 0x51). Под пределом выделение просто не удаётся:
// MuPDF бросает FZ_ERROR_SYSTEM «malloc (…) failed», для broken-ok это допустимая ошибка, а машина цела.
// Предел — OTTISK_TEST_MEMORY_LIMIT_MB (по умолчанию 2048, под ASan 4096; 0 — без предела).
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

#if defined(__SANITIZE_ADDRESS__)
constexpr unsigned long long kDefaultLimitMb = 4096;  // теневая память и карантин ASan
#else
constexpr unsigned long long kDefaultLimitMb = 2048;
#endif

struct MemoryCap {
    MemoryCap() {
        unsigned long long mb = kDefaultLimitMb;
        char value[32] = {};
        const DWORD n = GetEnvironmentVariableA("OTTISK_TEST_MEMORY_LIMIT_MB", value, sizeof value);
        if (n > 0 && n < sizeof value) {
            mb = std::strtoull(value, nullptr, 10);
        }
        if (mb == 0) {
            return;
        }
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            std::fprintf(stderr, "engine_tests: CreateJobObjectW: %lu\n", GetLastError());
            return;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        info.ProcessMemoryLimit = static_cast<SIZE_T>(mb) * 1024 * 1024;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info) ||
            !AssignProcessToJobObject(job, GetCurrentProcess())) {
            std::fprintf(stderr, "engine_tests: memory limit %llu MB not set: %lu\n", mb, GetLastError());
            CloseHandle(job);
            return;
        }
        // Описатель не закрываем: задание живёт, пока жив процесс.
    }
};

const MemoryCap kMemoryCap;

}  // namespace
