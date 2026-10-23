#include "core/optimize/qpdf_tool.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <system_error>

#include "core/util/paths.h"

namespace ot {

namespace fs = std::filesystem;

namespace {

constexpr int kCheckTimeoutMs = 120'000;
constexpr int kRewriteTimeoutMs = 600'000;

// Аргумент командной строки по правилам CommandLineToArgvW (кавычки; обратные слэши перед кавычкой
// удваиваются).
std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(arg[i]);
    }
    out.push_back(L'"');
    return out;
}

std::wstring envVar(const wchar_t* name) {
    const DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) {
        return {};
    }
    std::wstring value(n, L'\0');
    const DWORD got = GetEnvironmentVariableW(name, value.data(), n);
    value.resize(got);
    return value;
}

fs::path exeDir() {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
}

void addPassword(std::vector<std::wstring>& args, const std::string& password) {
    if (!password.empty()) {
        args.push_back(L"--password=" + paths::widen(password));
    }
}

struct HandleCloser {
    HANDLE h = nullptr;
    ~HandleCloser() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};

}  // namespace

fs::path QpdfTool::find() {
    std::vector<fs::path> candidates;
    const std::wstring tools = envVar(L"OTTISK_TOOLS");
    if (!tools.empty()) {
        candidates.push_back(fs::path(tools) / L"qpdf" / L"bin" / L"qpdf.exe");
        candidates.push_back(fs::path(tools) / L"qpdf.exe");
    }
    const fs::path dir = exeDir();
    candidates.push_back(dir / L"qpdf.exe");
    candidates.push_back(dir / L"qpdf" / L"bin" / L"qpdf.exe");
    candidates.emplace_back(L"C:\\Tools\\qpdf\\bin\\qpdf.exe");
    for (const fs::path& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) {
            return c;
        }
    }
    return {};
}

QpdfTool::QpdfTool(fs::path exe) : exe_(std::move(exe)) {}

ToolResult QpdfTool::check(const fs::path& file, const std::string& password) const {
    std::vector<std::wstring> args{L"--check"};
    addPassword(args, password);
    args.push_back(file.native());
    return run(args, kCheckTimeoutMs);
}

ToolResult QpdfTool::linearize(const fs::path& in, const fs::path& out, const std::string& password) const {
    std::vector<std::wstring> args;
    addPassword(args, password);
    args.push_back(L"--linearize");
    args.push_back(in.native());
    args.push_back(out.native());
    return run(args, kRewriteTimeoutMs);
}

ToolResult QpdfTool::checkLinearization(const fs::path& file, const std::string& password) const {
    std::vector<std::wstring> args{L"--check-linearization"};
    addPassword(args, password);
    args.push_back(file.native());
    return run(args, kCheckTimeoutMs);
}

ToolResult QpdfTool::json(const fs::path& file, const std::string& key, const std::string& password) const {
    std::vector<std::wstring> args{L"--json=2"};
    if (!key.empty()) {
        args.push_back(L"--json-key=" + paths::widen(key));
    }
    addPassword(args, password);
    args.push_back(file.native());
    return run(args, kCheckTimeoutMs);
}

ToolResult QpdfTool::run(const std::vector<std::wstring>& args, int timeoutMs) const {
    ToolResult result;
    if (exe_.empty()) {
        result.output = "qpdf.exe not found";
        return result;
    }
    std::wstring cmd = quoteArg(exe_.native());
    for (const std::wstring& a : args) {
        cmd += L' ';
        cmd += quoteArg(a);
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HandleCloser readPipe;
    HandleCloser writePipe;
    if (CreatePipe(&readPipe.h, &writePipe.h, &sa, 0) == 0) {
        result.output = "CreatePipe: " + std::system_category().message(static_cast<int>(GetLastError()));
        return result;
    }
    SetHandleInformation(readPipe.h, HANDLE_FLAG_INHERIT, 0);
    HandleCloser nul;
    nul.h =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // Наследуются только NUL и запись канала (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), без окна консоли.
    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    std::vector<unsigned char> listBuf(listSize);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBuf.data());
    InitializeProcThreadAttributeList(list, 1, 0, &listSize);
    HANDLE inherit[2] = {nul.h, writePipe.h};
    UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr,
                              nullptr);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul.h;
    si.StartupInfo.hStdOutput = writePipe.h;
    si.StartupInfo.hStdError = writePipe.h;
    si.lpAttributeList = list;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                        &si.StartupInfo, &pi);
    const DWORD startError = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (started == 0) {
        result.output = "CreateProcess " + paths::toUtf8(exe_) + ": " +
                        std::system_category().message(static_cast<int>(startError));
        return result;
    }
    HandleCloser process{pi.hProcess};
    HandleCloser thread{pi.hThread};
    CloseHandle(writePipe.h);  // запись канала — только у процесса
    writePipe.h = nullptr;

    // Читать, пока процесс жив (не до EOF: канал мог унаследовать чужой процесс), затем дочитать остаток.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool exited = false;
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(readPipe.h, nullptr, 0, nullptr, &avail, nullptr) != 0 && avail > 0) {
            char buf[8192];
            DWORD got = 0;
            if (ReadFile(readPipe.h, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) == 0 ||
                got == 0) {
                break;
            }
            result.output.append(buf, got);
        }
        if (exited) {
            break;
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            exited = true;
            continue;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            result.exitCode = -2;
            result.output += "\nqpdf: timed out";
            return result;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = static_cast<int>(code);
    return result;
}

}  // namespace ot
