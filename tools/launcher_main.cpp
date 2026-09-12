#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace
{
std::wstring QuoteArgument(const std::wstring& value)
{
    if(value.empty()) return L"\"\"";

    const bool needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if(!needsQuotes) return value;

    std::wstring out;
    out.push_back(L'\"');
    unsigned backslashes = 0;
    for(const wchar_t ch : value)
    {
        if(ch == L'\\')
        {
            ++backslashes;
            continue;
        }

        if(ch == L'\"')
        {
            out.append(backslashes * 2u + 1u, L'\\');
            out.push_back(L'\"');
            backslashes = 0;
            continue;
        }

        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2u, L'\\');
    out.push_back(L'\"');
    return out;
}

std::wstring LauncherDirectory()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if(length == 0 || length >= buffer.size()) return {};

    std::wstring path(buffer.data(), length);
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

void ShowLaunchError(const std::wstring& message)
{
    MessageBoxW(nullptr,
                message.c_str(),
                L"BO3 Shader Studio",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const std::wstring root = LauncherDirectory();
    if(root.empty())
    {
        ShowLaunchError(L"Shader Studio could not determine its installation folder.");
        return 1;
    }

    const std::wstring runtimeDir = root + L"\\runtime";
    const std::wstring runtimeExe = runtimeDir + L"\\BO3HLSLPreviewer.exe";

    const DWORD attributes = GetFileAttributesW(runtimeExe.c_str());
    if(attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        ShowLaunchError(L"Shader Studio's runtime is missing.\n\nExpected:\n" + runtimeExe +
                        L"\n\nRe-extract the complete BO3 Shader Studio ZIP.");
        return 2;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if(!argv)
    {
        ShowLaunchError(L"Shader Studio could not read its launch arguments.");
        return 4;
    }

    std::wstring commandLine = QuoteArgument(runtimeExe);
    for(int i = 1; i < argc; ++i)
    {
        commandLine.push_back(L' ');
        commandLine += QuoteArgument(argv[i]);
    }
    LocalFree(argv);

    // CreateProcessW is allowed to modify its command-line buffer.
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(runtimeExe.c_str(),
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        0,
                                        nullptr,
                                        runtimeDir.c_str(),
                                        &startup,
                                        &process);
    if(!started)
    {
        const DWORD error = GetLastError();
        ShowLaunchError(L"Shader Studio could not start its runtime.\n\nWindows error: " +
                        std::to_wstring(error) +
                        L"\n\nTry re-extracting the complete release ZIP.");
        return 3;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
