#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Shared FXC include resolver used by both the preview renderer and the
// application-side validation/compile paths. Keeping this in one header avoids
// translation-unit coupling after main.cpp was split into smaller modules.
class ShaderIncludeHandler final : public ID3DInclude
{
public:
    ShaderIncludeHandler(std::filesystem::path baseDir, std::filesystem::path explicitRoot)
        : baseDir_(std::move(baseDir)), explicitRoot_(std::move(explicitRoot))
    {
        AddSearchRoot(baseDir_);
        if (!explicitRoot_.empty()) AddSearchRoot(explicitRoot_);

        std::filesystem::path walk = baseDir_;
        for (int i = 0; i < 5 && !walk.empty(); ++i)
        {
            AddSearchRoot(walk);
            AddSearchRoot(walk / L"include");
            AddSearchRoot(walk / L"includes");
            AddSearchRoot(walk / L"lib");
            AddSearchRoot(walk / L"common");
            AddSearchRoot(walk / L"shaders");
            AddSearchRoot(walk / L"shaders_stable");
            walk = walk.parent_path();
        }

        const std::filesystem::path bundled = ExecutableDirectory() / L"bo3_compat" / L"shaders_stable";
        AddSearchRoot(bundled);
        AddSearchRoot(bundled / L"postfx");
        AddSearchRoot(bundled / L"lib");
        AddSearchRoot(bundled / L"code");
        AddSearchRoot(bundled / L"gfxcore");

        const std::filesystem::path cwdBundled =
            std::filesystem::current_path() / L"bo3_compat" / L"shaders_stable";
        AddSearchRoot(cwdBundled);
        AddSearchRoot(cwdBundled / L"postfx");
        AddSearchRoot(cwdBundled / L"lib");
        AddSearchRoot(cwdBundled / L"code");
        AddSearchRoot(cwdBundled / L"gfxcore");

        AddSearchRoot(std::filesystem::current_path());
    }

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentData,
                           LPCVOID* data, UINT* bytes) override
    {
        if (!fileName || !data || !bytes) return E_INVALIDARG;
        const std::filesystem::path requested = Utf8ToWide(fileName);

        std::vector<std::filesystem::path> candidates;
        auto addCandidate = [&](const std::filesystem::path& p)
        {
            if (p.empty()) return;
            const std::filesystem::path normalized = p.lexically_normal();
            if (std::find(candidates.begin(), candidates.end(), normalized) == candidates.end())
                candidates.push_back(normalized);
        };

        if (parentData)
        {
            auto it = allocations_.find(parentData);
            if (it != allocations_.end())
                addCandidate(it->second.path.parent_path() / requested);
        }

        for (const auto& root : searchRoots_)
        {
            addCandidate(root / requested);
            addCandidate(root / L"include" / requested);
            addCandidate(root / L"includes" / requested);
            addCandidate(root / L"lib" / requested);
            addCandidate(root / L"common" / requested);
        }

        std::filesystem::path found;
        for (const auto& candidate : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                found = candidate;
                break;
            }
        }

        if (found.empty())
        {
            MissingInclude miss{};
            miss.name = requested.wstring();
            miss.candidates = std::move(candidates);
            missing_.push_back(std::move(miss));
            return E_FAIL;
        }

        std::ifstream in(found, std::ios::binary);
        if (!in) return E_FAIL;
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string content = ss.str();

        Allocation allocation{};
        allocation.data = std::make_unique<char[]>(content.size() + 1);
        std::memcpy(allocation.data.get(), content.data(), content.size());
        allocation.data[content.size()] = '\0';
        allocation.path = found;

        const void* key = allocation.data.get();
        *data = key;
        *bytes = static_cast<UINT>(content.size());
        allocations_.emplace(key, std::move(allocation));
        return S_OK;
    }

    HRESULT __stdcall Close(LPCVOID data) override
    {
        allocations_.erase(data);
        return S_OK;
    }

    std::wstring MissingDiagnostics() const
    {
        if (missing_.empty()) return {};
        std::wstring result =
            L"\r\n[Preview] Include resolver could not find one or more files.\r\n"
            L"[Preview] Nested includes are resolved relative to their parent header first.\r\n";

        std::unordered_set<std::wstring> shown;
        for (const auto& miss : missing_)
        {
            if (!shown.insert(miss.name).second) continue;
            result += L"\r\n  Missing: ";
            result += miss.name;
            result += L"\r\n  Searched:\r\n";
            const size_t limit = std::min<size_t>(miss.candidates.size(), 12);
            for (size_t i = 0; i < limit; ++i)
            {
                result += L"    ";
                result += miss.candidates[i].wstring();
                result += L"\r\n";
            }
            if (miss.candidates.size() > limit)
            {
                result += L"    ... and ";
                result += std::to_wstring(miss.candidates.size() - limit);
                result += L" more locations\r\n";
            }
        }

        result +=
            L"\r\n[Preview] If the BO3 header tree is somewhere else, use Include Root... "
            L"and select the folder that contains folders such as lib\\ or include\\.\r\n";
        return result;
    }

private:
    struct Allocation
    {
        std::unique_ptr<char[]> data;
        std::filesystem::path path;
    };

    struct MissingInclude
    {
        std::wstring name;
        std::vector<std::filesystem::path> candidates;
    };

    static std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring out(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
        return out;
    }

    static std::filesystem::path ExecutableDirectory()
    {
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) return std::filesystem::current_path();
        return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
    }

    void AddSearchRoot(const std::filesystem::path& root)
    {
        if (root.empty()) return;
        const std::filesystem::path normalized = root.lexically_normal();
        if (std::find(searchRoots_.begin(), searchRoots_.end(), normalized) == searchRoots_.end())
            searchRoots_.push_back(normalized);
    }

    std::filesystem::path baseDir_;
    std::filesystem::path explicitRoot_;
    std::vector<std::filesystem::path> searchRoots_;
    std::unordered_map<const void*, Allocation> allocations_;
    std::vector<MissingInclude> missing_;
};
