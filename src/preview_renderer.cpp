#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "preview_renderer.h"

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <DirectXMath.h>

#include <QImage>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model_import.h"
#include "hlsl_preview_mode.h"
#include "shader_include_handler.h"

#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_PIZ 1
#define TINYEXR_USE_THREAD 0
#include "tinyexr.h"

#ifdef getChar
#undef getChar
#endif

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace
{
struct MaterialVertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 tangent;
    DirectX::XMFLOAT2 uv;
};

struct PreviewMeshBuffers
{
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> ib;
    UINT indexCount = 0;
};




std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

fs::path GetExecutableDirectory()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return fs::current_path();
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}


struct ReflectedVariable
{
    std::string name;
    UINT offset = 0;
    UINT size = 0;
    D3D_SHADER_VARIABLE_CLASS varClass = D3D_SVC_SCALAR;
    D3D_SHADER_VARIABLE_TYPE varType = D3D_SVT_FLOAT;
    UINT rows = 1;
    UINT columns = 1;
    UINT elements = 0;
};


struct ReflectedCBuffer
{
    UINT slot = 0;
    UINT byteSize = 0;
    ComPtr<ID3D11Buffer> buffer;
    std::vector<ReflectedVariable> variables;
};

struct ReflectedResource
{
    std::string name;
    UINT slot = 0;
    UINT bindCount = 1;
    D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
};


} // namespace

class PreviewRenderer::Impl
{
public:
    bool Initialize(HWND hwnd, std::wstring& error)
    {
        hwnd_ = hwnd;
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        width_ = std::max(1L, rc.right - rc.left);
        height_ = std::max(1L, rc.bottom - rc.top);

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Width = static_cast<UINT>(width_);
        sd.BufferDesc.Height = static_cast<UINT>(height_);
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.OutputWindow = hwnd_;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        // Windows Graphics Capture requires a BGRA-capable D3D11 device. This
        // flag is harmless for normal preview rendering and keeps captured
        // window frames GPU-native.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL obtained{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, 1, D3D11_SDK_VERSION, &sd,
            swapChain_.GetAddressOf(), device_.GetAddressOf(), &obtained, context_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"D3D11CreateDeviceAndSwapChain failed (0x" + Hex(hr) + L")";
            return false;
        }

        QueryAdapterName();
        InitializeGpuTimers();
        if (!CreateRenderTarget(error)) return false;
        if (!CreateBuiltInVertexShaders(error)) return false;
        if (!CreateCameraBuffer(error)) return false;
        if (!CreateMaterialCameraBuffer(error)) return false;
        if (!CreateDeferredLightBuffer(error)) return false;
        if (!CreateSamplers(error)) return false;
        if (!CreateMaterialRasterizer(error)) return false;
        if (!CreateDefaultTextures(error)) return false;
        if (!CreateGBufferTargets(error)) return false;
        if (!CreatePreviewMeshes(error)) return false;
        if (!CreateDefaultStudioEnvironment(error)) return false;
        fpsWindowStart_ = std::chrono::steady_clock::now();
        return true;
    }

    void Resize(UINT w, UINT h)
    {
        if (!swapChain_ || !context_ || w == 0 || h == 0) return;
        if (w == static_cast<UINT>(width_) && h == static_cast<UINT>(height_)) return;

        // A native Qt child window can receive resize events a frame later than
        // its parent splitter. Fully detach every back-buffer/GBuffer reference
        // before ResizeBuffers so the swap chain always matches the real HWND.
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> nullSrvs{};
        context_->PSSetShaderResources(0, static_cast<UINT>(nullSrvs.size()), nullSrvs.data());
        context_->Flush();

        renderTarget_.Reset();
        for (auto& rtv : gbufferRTVs_) rtv.Reset();
        for (auto& srv : gbufferSRVs_) srv.Reset();
        for (auto& texture : gbufferTextures_) texture.Reset();
        materialDepthSRV_.Reset();
        materialDepthDSV_.Reset();
        materialDepthTexture_.Reset();

        if (FAILED(swapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return;

        width_ = static_cast<LONG>(w);
        height_ = static_cast<LONG>(h);
        std::wstring ignored;
        if (!CreateRenderTarget(ignored)) return;
        CreateGBufferTargets(ignored);
    }

    bool CompilePixelShader(const std::string& userSource, const fs::path& sourcePath,
                            const fs::path& includeRoot,
                            const std::string& entryPoint, const std::string& profile,
                            bool injectBO3Globals, std::wstring& errors)
    {
        if (!device_) return false;

        const fs::path includeBase = sourcePath.empty() ? fs::current_path() : sourcePath.parent_path();
        const std::string sourceName = sourcePath.empty() ? "editor.hlsl" : WideToUtf8(sourcePath.wstring());
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

        auto containsEntryFunction = [&](const char* name) -> bool
        {
            const size_t nameLen = std::strlen(name);
            size_t pos = 0;
            while ((pos = userSource.find(name, pos)) != std::string::npos)
            {
                const bool leftBoundary = pos == 0 || !(std::isalnum(static_cast<unsigned char>(userSource[pos - 1])) || userSource[pos - 1] == '_');
                size_t after = pos + nameLen;
                while (after < userSource.size() && std::isspace(static_cast<unsigned char>(userSource[after]))) ++after;
                const bool looksLikeFunction = after < userSource.size() && userSource[after] == '(';
                if (leftBoundary && looksLikeFunction) return true;
                pos += nameLen;
            }
            return false;
        };
        const bool hasVsMain = containsEntryFunction("vs_main");
        const bool hasPsMain = containsEntryFunction("ps_main");
        const bool hasMultipassPixelEntry =
            containsEntryFunction("ps_image") ||
            containsEntryFunction("ps_buffer_a") ||
            containsEntryFunction("ps_buffer_b") ||
            containsEntryFunction("ps_buffer_c") ||
            containsEntryFunction("ps_buffer_d");
        // A single-file multipass shader legitimately has vs_main plus several
        // pixel entry points without a function literally named ps_main. The old
        // heuristic misclassified that layout as a vertex-only material shader,
        // which then exposed BO3 INSTANCE_SEMANTIC/TEXCOORD15 as a mesh attribute.
        const bool vertexOnlyRequest = hasVsMain && !hasPsMain && !hasMultipassPixelEntry;
        const std::string compileEntry = vertexOnlyRequest
            ? "vs_main"
            : (!hasPsMain && containsEntryFunction("ps_image") ? "ps_image" : entryPoint);
        const std::string compileProfile = vertexOnlyRequest ? "vs_5_0" : profile;

        // The Previewer used to leave TOOLSGFX undefined. That accidentally
        // selected BO3's runtime postfx branch while still binding an ordinary
        // 0..1 preview image, which makes PostFx_NormalizeColor divide the image
        // by 32768 and is a confirmed cause of BO3-working shaders previewing
        // incorrectly/black. Make the preview context explicit.
        const bool postFxCompile = previewMode_ == PreviewMode::PostFX && !vertexOnlyRequest;
        const char* toolsgfxValue = postFxPreviewContext_ == PostFxPreviewContext::ToolsgfxMaterial ? "1" : "0";
        const D3D_SHADER_MACRO postFxMacros[] = {
            {"TOOLSGFX", toolsgfxValue},
            {nullptr, nullptr}
        };
        const D3D_SHADER_MACRO* compileMacros = postFxCompile ? postFxMacros : nullptr;

        auto compileSource = [&](const std::string& source, ComPtr<ID3DBlob>& bytecode, std::wstring& diagnostics) -> HRESULT
        {
            ShaderIncludeHandler include(includeBase, includeRoot);
            ComPtr<ID3DBlob> errorBlob;
            bytecode.Reset();
            HRESULT hr = D3DCompile(
                source.data(), source.size(), sourceName.c_str(), compileMacros, &include,
                compileEntry.c_str(), compileProfile.c_str(), flags, 0,
                bytecode.GetAddressOf(), errorBlob.GetAddressOf());

            if (errorBlob)
            {
                const std::string text(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                diagnostics = Utf8ToWide(text);
            }
            else
            {
                diagnostics.clear();
            }

            const std::wstring includeDiagnostics = include.MissingDiagnostics();
            if (!includeDiagnostics.empty())
                diagnostics += includeDiagnostics;
            return hr;
        };

        // First compile the BO3 shader exactly as it was written. Real BO3
        // shaders frequently already declare gameTime/renderTargetSize in engine
        // constant buffers, so preview helpers must never be injected blindly.
        ComPtr<ID3DBlob> bytecode;
        std::wstring firstDiagnostics;
        HRESULT hr = compileSource(userSource, bytecode, firstDiagnostics);
        std::wstring autoGlobalsMessage;

        if (FAILED(hr) && injectBO3Globals)
        {
            // D3DCompile often reports only the first undeclared identifier in a
            // pass. The old previewer retried once, which meant a shader using both
            // gameTime and renderTargetSize could fail on the *second* missing name.
            // Retry iteratively until every known BO3 preview global requested by
            // the compiler has been supplied, or until no new known name is found.
            struct AutoGlobal { const char* name; const char* type; };
            static constexpr AutoGlobal knownGlobals[] = {
                {"renderTargetSize", "float4"},
                {"gameTime",         "float4"},
                {"previewMouse",     "float4"},
                {"previewParams",    "float4"},
            };

            std::vector<AutoGlobal> injected;
            for (int attempt = 0; attempt < 8 && FAILED(hr); ++attempt)
            {
                const std::string diagnosticsUtf8 = WideToUtf8(firstDiagnostics);
                const bool hasUndeclared = diagnosticsUtf8.find("X3004") != std::string::npos ||
                                           diagnosticsUtf8.find("undeclared identifier") != std::string::npos;
                if (!hasUndeclared) break;

                bool added = false;
                for (const auto& global : knownGlobals)
                {
                    const bool alreadyInjected = std::any_of(
                        injected.begin(), injected.end(),
                        [&](const AutoGlobal& value){ return _stricmp(value.name, global.name) == 0; });
                    if (alreadyInjected) continue;

                    const std::string quoted1 = std::string("'") + global.name + "'";
                    const std::string quoted2 = std::string("\"") + global.name + "\"";
                    const bool namesIdentifier =
                        diagnosticsUtf8.find(quoted1) != std::string::npos ||
                        diagnosticsUtf8.find(quoted2) != std::string::npos ||
                        diagnosticsUtf8.find(global.name) != std::string::npos;
                    if (namesIdentifier)
                    {
                        injected.push_back(global);
                        added = true;
                    }
                }
                if (!added) break;

                std::string retrySource;
                retrySource += "cbuffer BO3PreviewAutoGlobals : register(b13)\n{\n";
                for (const auto& global : injected)
                {
                    retrySource += "    ";
                    retrySource += global.type;
                    retrySource += " ";
                    retrySource += global.name;
                    retrySource += ";\n";
                }
                retrySource += "};\n#line 1 \"user_shader.hlsl\"\n";
                retrySource += userSource;

                ComPtr<ID3DBlob> retryBytecode;
                std::wstring retryDiagnostics;
                hr = compileSource(retrySource, retryBytecode, retryDiagnostics);
                firstDiagnostics = retryDiagnostics;
                if (SUCCEEDED(hr))
                    bytecode = retryBytecode;
            }

            if (SUCCEEDED(hr) && !injected.empty())
            {
                autoGlobalsMessage = L"[HLSL Preview] Added synthetic Previewer-only globals: ";
                for (size_t i = 0; i < injected.size(); ++i)
                {
                    if (i) autoGlobalsMessage += L", ";
                    autoGlobalsMessage += Utf8ToWide(injected[i].name);
                }
                autoGlobalsMessage += L".\r\n";
            }
        }

        errors = firstDiagnostics;
        if (FAILED(hr)) return false;

        ComPtr<ID3D11PixelShader> newPS;
        ComPtr<ID3D11VertexShader> newUserVS;
        ComPtr<ID3D11InputLayout> newUserLayout;
        if (vertexOnlyRequest)
        {
            hr = device_->CreateVertexShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, newUserVS.GetAddressOf());
            if (FAILED(hr))
            {
                errors += L"\r\nCreateVertexShader failed (0x" + Hex(hr) + L")";
                return false;
            }
            if (!CreateUserMaterialInputLayout(bytecode.Get(), newUserLayout, errors))
                return false;
            newPS = vertexOnlyMaterialPixelShader_;
        }
        else
        {
            hr = device_->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, newPS.GetAddressOf());
            if (FAILED(hr))
            {
                errors += L"\r\nCreatePixelShader failed (0x" + Hex(hr) + L")";
                return false;
            }
        }

        // Optional temporal auto-exposure state pass. A shader that declares
        // ps_exposure_state is compiled twice from the same source: ps_main for
        // the final image and ps_exposure_state for a persistent 1x1 feedback
        // target. This keeps the source as one HLSL file while giving effects
        // such as old-camera auto iris real previous-frame memory.
        ComPtr<ID3D11PixelShader> newTemporalExposureStatePS;
        bool newTemporalExposureMode = false;
        if (!vertexOnlyRequest && containsEntryFunction("ps_exposure_state"))
        {
            ShaderIncludeHandler stateInclude(includeBase, includeRoot);
            ComPtr<ID3DBlob> stateBytecode;
            ComPtr<ID3DBlob> stateErrorBlob;
            HRESULT stateHr = D3DCompile(
                userSource.data(), userSource.size(), sourceName.c_str(), compileMacros, &stateInclude,
                "ps_exposure_state", "ps_5_0", flags, 0,
                stateBytecode.GetAddressOf(), stateErrorBlob.GetAddressOf());

            if (FAILED(stateHr))
            {
                if (stateErrorBlob)
                {
                    const std::string text(static_cast<const char*>(stateErrorBlob->GetBufferPointer()), stateErrorBlob->GetBufferSize());
                    errors += L"\r\n[Temporal exposure state pass]\r\n" + Utf8ToWide(text);
                }
                const std::wstring includeDiagnostics = stateInclude.MissingDiagnostics();
                if (!includeDiagnostics.empty()) errors += includeDiagnostics;
                return false;
            }

            stateHr = device_->CreatePixelShader(
                stateBytecode->GetBufferPointer(), stateBytecode->GetBufferSize(),
                nullptr, newTemporalExposureStatePS.GetAddressOf());
            if (FAILED(stateHr))
            {
                errors += L"\r\nCreatePixelShader(ps_exposure_state) failed (0x" + Hex(stateHr) + L")";
                return false;
            }
            newTemporalExposureMode = true;
        }

        std::vector<ReflectedCBuffer> newCBuffers;
        std::vector<ReflectedResource> newResources;
        QSet<QString> newOptimizedPixelResourceNames;
        ShaderPerformanceStats newPerformanceStats{};
        ReflectConstantBuffers(bytecode.Get(), newCBuffers);
        ReflectTextureResources(bytecode.Get(), newResources);
        const bool newOptimizedPixelResourceReflectionValid =
            ReflectBindableResourceNames(bytecode.Get(), newOptimizedPixelResourceNames);
        ReflectShaderPerformance(bytecode.Get(), newPerformanceStats);
        // Material / Surface shaders commonly use float3/float4 TEXCOORD0 plus
        // TEXCOORD1 for world position. The old sky heuristic mistook that layout
        // for BO3 skyDirection/fogDirection and fed the mesh a directional vertex
        // shader instead of ordinary UVs (on a vertical Card this collapsed V to 0).
        // An explicit material marker always wins over directional-sky detection.
        const bool explicitMaterialSurface =
            userSource.find("BO3_PREVIEWER_MATERIAL_SURFACE:") != std::string::npos;
        const bool adaptedMaterialSurface =
            userSource.find("Generated by BO3 HLSL Previewer - Custom HLSL Material adapter") != std::string::npos;
        const bool newSkyMode = vertexOnlyRequest ? false :
            (!explicitMaterialSurface && DetectSkyShaderInput(bytecode.Get()));
        const bool newDeferredMaterialShader = !vertexOnlyRequest &&
            (explicitMaterialSurface || adaptedMaterialSurface) &&
            DetectDeferredMaterialOutput(bytecode.Get());

        bool hasDownSamplesVariable = false;
        for (const auto& cb : newCBuffers)
        {
            for (const auto& v : cb.variables)
            {
                if (_stricmp(v.name.c_str(), "downSamples") == 0)
                {
                    hasDownSamplesVariable = true;
                    break;
                }
            }
            if (hasDownSamplesVariable) break;
        }
        const bool usesFramebufferLoad = userSource.find("frameBuffer.Load") != std::string::npos ||
                                         userSource.find("framebuffer.Load") != std::string::npos;
        const bool newDownsamplePass = !vertexOnlyRequest && hasDownSamplesVariable && usesFramebufferLoad;
        const bool newUpsamplingPass = newDownsamplePass &&
            (userSource.find("#define UPSAMPLING 1") != std::string::npos ||
             userSource.find("#define UPSAMPLING	1") != std::string::npos);

        std::array<bool, 8> detectedVectors{};
        for (const auto& cb : newCBuffers)
        {
            for (const auto& v : cb.variables)
            {
                for (int i = 0; i < 8; ++i)
                {
                    const std::string expected = "scriptVector" + std::to_string(i);
                    if (_stricmp(v.name.c_str(), expected.c_str()) == 0)
                    {
                        detectedVectors[static_cast<size_t>(i)] = true;
                        break;
                    }
                }
            }
        }

        ++shaderGeneration_;
        gpuPassMs_ = 0.0f;
        pixelShader_ = newPS;
        temporalExposureStatePS_ = newTemporalExposureStatePS;
        temporalExposureMode_ = newTemporalExposureMode;
        ResetTemporalExposureState();
        userVertexShader_ = newUserVS;
        userMaterialInputLayout_ = newUserLayout;
        vertexOnlyShader_ = vertexOnlyRequest;
        if (vertexOnlyShader_ && (previewMode_ == PreviewMode::HLSL ||
                                  previewMode_ == PreviewMode::PostFX ||
                                  previewMode_ == PreviewMode::Sky))
            previewMode_ = PreviewMode::ForwardMaterial;
        cbuffers_ = std::move(newCBuffers);
        resources_ = std::move(newResources);
        optimizedPixelResourceNames_ = std::move(newOptimizedPixelResourceNames);
        optimizedPixelResourceReflectionValid_ = newOptimizedPixelResourceReflectionValid;
        ApplyPreviewParameterDefaults();
        shaderPerformanceStats_ = newPerformanceStats;
        skyShaderMode_ = newSkyMode;
        adaptedMaterialShader_ = adaptedMaterialSurface;
        deferredMaterialShader_ = newDeferredMaterialShader;
        if (!vertexOnlyShader_ &&
            (previewMode_ == PreviewMode::ForwardMaterial || previewMode_ == PreviewMode::DeferredGBuffer))
            previewMode_ = deferredMaterialShader_ ? PreviewMode::DeferredGBuffer : PreviewMode::ForwardMaterial;
        downsamplePassDetected_ = newDownsamplePass;
        upsamplingPassDetected_ = newUpsamplingPass;
        downSamplesValue_ = downsamplePassDetected_ ? 1.0f : 0.0f;
        detectedScriptVectors_ = detectedVectors;

        std::wstring previewMessage = autoGlobalsMessage;
        if (!hasPsMain && hasMultipassPixelEntry && containsEntryFunction("ps_image"))
            previewMessage += L"[Preview] Single-file multipass HLSL detected: compiling ps_image as the final preview entry point. Intermediate buffer execution still requires the multipass runtime.\r\n";
        if (temporalExposureMode_)
            previewMessage += L"[Preview] Temporal auto exposure ACTIVE (PREVIEW-ONLY): ps_exposure_state -> private persistent 1x1 D3D11 ping-pong history -> ps_main. Current BO3 PostFX export does not create this history resource.\r\n";
        if (vertexOnlyShader_)
            previewMessage += L"[Preview] Vertex-displacement shader detected: compiled vs_main as vs_5_0 and attached the built-in material/GBuffer pixel shader.\r\n";
        if (explicitMaterialSurface)
        {
            previewMessage += L"[Preview] Material / Surface marker detected: mesh UV vertex path forced (sky-direction detection disabled).\r\n";
            if (userSource.find("BO3_PREVIEWER_GLSL_PROJECTION: SEAMLESS_TRIPLANAR_V2") == std::string::npos &&
                (userSource.find("BO3GLSL_EvaluateMaterialSeamSafe") != std::string::npos ||
                 userSource.find("BO3GLSL_StabilizeMaterialPoles") != std::string::npos ||
                 userSource.find("BO3GLSL_PeriodicMaterialUWeight") != std::string::npos))
            {
                previewMessage += L"[Preview] WARNING: Legacy converted GLSL Material projection detected. This HLSL still embeds the old UV seam/pole wrapper; reconvert the original GLSL with the current converter to receive SEAMLESS_TRIPLANAR_V2.\r\n";
            }
        }
        if (userSource.find("BO3_PREVIEWER_SKY_SOURCE: IMAGE_SPACE_LATLONG") != std::string::npos)
            previewMessage += L"[Preview] WARNING: Legacy image-space Sky wrapper detected. Reconvert the original GLSL to replace lat-long seam/pole mapping with IMAGE_SPACE_SEAMLESS_3D.\r\n";
        if (postFxCompile)
        {
            if (postFxPreviewContext_ == PostFxPreviewContext::ToolsgfxMaterial)
                previewMessage += L"[Preview] BO3 PostFX context: TOOLSGFX / material preview (TOOLSGFX=1, ordinary 0..1 source image).\r\n";
            else
            {
                const float runtimeExposureScale = std::exp2(postFxRuntimeExposureEV_);
                if (sourceEncoding_ == PreviewSourceEncoding::HdrSceneLinear)
                {
                    previewMessage += L"[Preview] BO3 PostFX context: runtime resolvedScene simulation (TOOLSGFX=0, TRUE scene-linear HDR source -> 32768 scale -> runtime HLSL -> display transfer).\r\n";
                    previewMessage += L"[Preview] HDR source statistics: mean luminance=" + std::to_wstring(sourceHdrMeanLuminance_) +
                                      L", peak luminance=" + std::to_wstring(sourceHdrPeakLuminance_) + L".\r\n";
                }
                else
                {
                    previewMessage += L"[Preview] BO3 PostFX context: runtime resolvedScene LDR APPROXIMATION (TOOLSGFX=0, sRGB -> linear -> 32768 scale -> runtime HLSL -> display transfer).\r\n";
                    previewMessage += L"[Preview] WARNING: PNG/JPEG/BMP/TIFF cannot preserve resolvedScene HDR values above 1.0. Exact BO3 highlight/exposure parity requires a scene-linear EXR source.\r\n";
                }
                previewMessage += L"[Preview] Runtime scene exposure: " + std::to_wstring(postFxRuntimeExposureEV_) +
                                  L" EV (scale " + std::to_wstring(runtimeExposureScale) + L").\r\n";
            }
        }
        if (previewMode_ == PreviewMode::DeferredGBuffer)
            previewMessage += L"[Preview] Input mode: DEFERRED / GBUFFER (3D mesh + MRT inspector + deferred light preview).\r\n";
        else if (previewMode_ == PreviewMode::ForwardMaterial)
            previewMessage += L"[Preview] Input mode: FORWARD MATERIAL (3D mesh rendered directly to the final color buffer).\r\n";
        else if (newSkyMode)
            previewMessage += L"[Preview] Input mode: SKY / directional 3D (drag Preview to look around; wheel changes FOV).\r\n";
        else
            previewMessage += L"[Preview] Input mode: POST FX / 2D (fullscreen UVs).\r\n";
        if (downsamplePassDetected_)
        {
            previewMessage += upsamplingPassDetected_
                ? L"[Preview] BO3 upsample pass detected: native-resolution offscreen pass emulation enabled (downSamples=1).\r\n"
                : L"[Preview] BO3 downsample pass detected: native-resolution offscreen pass emulation enabled (2x reduction, downSamples=1).\r\n";
        }
        if (HasReflectedVariable("radius") && HasReflectedVariable("density") && HasReflectedVariable("intensity"))
        {
            previewMessage += L"[Preview] Runtime shader parameters detected. Fisheye preview defaults are active; adjust them in Shader Parameters.\r\n";
        }
        if (bo3NeutralDefaults_)
        {
            previewMessage += L"[Preview] Neutral BO3 script-vector defaults are active (runtime preset values).\r\n";
        }
        UINT highestTextureSlot = 0;
        for (const auto& resource : resources_)
            highestTextureSlot = std::max(highestTextureSlot, resource.slot + std::max(1u, resource.bindCount) - 1u);
        if (highestTextureSlot > 15)
        {
            previewMessage += L"[Preview] BO3 high texture slots detected through t" + std::to_wstring(highestTextureSlot);
            previewMessage += L"; reflected neutral fallbacks are available through t127.\r\n";
        }
        if (!errors.empty() && errors.front() != L'\r' && errors.front() != L'\n')
            previewMessage += L"\r\n";
        errors = previewMessage + errors;
        return true;
    }

    bool LoadEnvironmentTexture(const fs::path& path, std::wstring& error)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (extension == ".exr")
        {
            float* exrPixels = nullptr;
            int exrWidth = 0;
            int exrHeight = 0;
            const char* exrError = nullptr;
            const std::string utf8Path = WideToUtf8(path.wstring());
            const int result = LoadEXR(&exrPixels, &exrWidth, &exrHeight, utf8Path.c_str(), &exrError);
            if (result != TINYEXR_SUCCESS || !exrPixels || exrWidth <= 0 || exrHeight <= 0)
            {
                if (exrError)
                {
                    error = L"Could not decode EXR environment image: " + Utf8ToWide(exrError);
                    FreeEXRErrorMessage(exrError);
                }
                else
                {
                    error = L"Could not decode EXR environment image: " + path.wstring();
                }
                if (exrPixels) std::free(exrPixels);
                return false;
            }

            const UINT w = static_cast<UINT>(exrWidth);
            const UINT h = static_cast<UINT>(exrHeight);

            double sumR = 0.0, sumG = 0.0, sumB = 0.0, totalWeight = 0.0;
            double bestLum = -1.0;
            std::array<float, 3> brightestLinear{1.0f, 1.0f, 1.0f};
            for (UINT y = 0; y < h; ++y)
            {
                const double theta = (static_cast<double>(y) + 0.5) / static_cast<double>(h) * 3.14159265358979323846;
                const double rowWeight = std::max(0.001, std::sin(theta));
                for (UINT x = 0; x < w; ++x)
                {
                    const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                    const float r = std::max(0.0f, exrPixels[i + 0]);
                    const float g = std::max(0.0f, exrPixels[i + 1]);
                    const float b = std::max(0.0f, exrPixels[i + 2]);
                    exrPixels[i + 0] = r;
                    exrPixels[i + 1] = g;
                    exrPixels[i + 2] = b;
                    exrPixels[i + 3] = std::clamp(exrPixels[i + 3], 0.0f, 1.0f);

                    sumR += static_cast<double>(r) * rowWeight;
                    sumG += static_cast<double>(g) * rowWeight;
                    sumB += static_cast<double>(b) * rowWeight;
                    totalWeight += rowWeight;
                    const double lum = static_cast<double>(r) * 0.2126 + static_cast<double>(g) * 0.7152 + static_cast<double>(b) * 0.0722;
                    if (lum > bestLum)
                    {
                        bestLum = lum;
                        brightestLinear = {r, g, b};
                    }
                }
            }

            // Keep OpenEXR environment maps as floating-point linear HDR all the
            // way to the preview shader. The previous path baked a Reinhard-like
            // 8-bit image at load time, destroying the high-luminance range that
            // APE/TOOLSGFX relies on for sky reflections and specular response.
            //
            // Treyarch's Day/Sunset source lat-longs are 8192x4096. Uploading
            // those as RGBA32F would consume about 512 MiB of VRAM per sky, so
            // use a preview-sized HDR copy while keeping statistics from the
            // full-resolution source above. No clamping/tone mapping occurs.
            const UINT maxPreviewWidth = 2048u;
            const UINT maxPreviewHeight = 1024u;
            UINT uploadW = w;
            UINT uploadH = h;
            std::vector<float> reducedHdr;
            const float* uploadPixels = exrPixels;
            if (w > maxPreviewWidth || h > maxPreviewHeight)
            {
                const float scale = std::min(static_cast<float>(maxPreviewWidth) / static_cast<float>(w),
                                             static_cast<float>(maxPreviewHeight) / static_cast<float>(h));
                uploadW = std::max<UINT>(1u, static_cast<UINT>(std::lround(static_cast<float>(w) * scale)));
                uploadH = std::max<UINT>(1u, static_cast<UINT>(std::lround(static_cast<float>(h) * scale)));
                reducedHdr.resize(static_cast<size_t>(uploadW) * uploadH * 4u);

                for (UINT y = 0; y < uploadH; ++y)
                {
                    const float sourceY = ((static_cast<float>(y) + 0.5f) * static_cast<float>(h) /
                                           static_cast<float>(uploadH)) - 0.5f;
                    const UINT y0 = static_cast<UINT>(std::clamp(std::floor(sourceY), 0.0f, static_cast<float>(h - 1)));
                    const UINT y1 = std::min(h - 1, y0 + 1);
                    const float ty = std::clamp(sourceY - static_cast<float>(y0), 0.0f, 1.0f);
                    for (UINT x = 0; x < uploadW; ++x)
                    {
                        const float sourceX = ((static_cast<float>(x) + 0.5f) * static_cast<float>(w) /
                                               static_cast<float>(uploadW)) - 0.5f;
                        const UINT x0 = static_cast<UINT>(std::clamp(std::floor(sourceX), 0.0f, static_cast<float>(w - 1)));
                        const UINT x1 = std::min(w - 1, x0 + 1);
                        const float tx = std::clamp(sourceX - static_cast<float>(x0), 0.0f, 1.0f);
                        const size_t dst = (static_cast<size_t>(y) * uploadW + x) * 4u;
                        const size_t i00 = (static_cast<size_t>(y0) * w + x0) * 4u;
                        const size_t i10 = (static_cast<size_t>(y0) * w + x1) * 4u;
                        const size_t i01 = (static_cast<size_t>(y1) * w + x0) * 4u;
                        const size_t i11 = (static_cast<size_t>(y1) * w + x1) * 4u;
                        for (int c = 0; c < 4; ++c)
                        {
                            const float top = exrPixels[i00 + c] + (exrPixels[i10 + c] - exrPixels[i00 + c]) * tx;
                            const float bottom = exrPixels[i01 + c] + (exrPixels[i11 + c] - exrPixels[i01 + c]) * tx;
                            reducedHdr[dst + c] = top + (bottom - top) * ty;
                        }
                    }
                }
                uploadPixels = reducedHdr.data();
            }

            ComPtr<ID3D11ShaderResourceView> srv;
            if (!CreateFloatTextureSRV(uploadPixels, uploadW, uploadH, srv, error))
            {
                std::free(exrPixels);
                return false;
            }

            if (totalWeight > 0.0)
            {
                environmentAverageColor_ = {
                    static_cast<float>(sumR / totalWeight),
                    static_cast<float>(sumG / totalWeight),
                    static_cast<float>(sumB / totalWeight)
                };
            }
            const float maxSun = std::max({brightestLinear[0], brightestLinear[1], brightestLinear[2], 0.0001f});
            environmentSunColor_ = {
                std::clamp(brightestLinear[0] / maxSun, 0.0f, 1.0f),
                std::clamp(brightestLinear[1] / maxSun, 0.0f, 1.0f),
                std::clamp(brightestLinear[2] / maxSun, 0.0f, 1.0f)
            };
            environmentSRV_ = srv;
            environmentPath_ = path;
            environmentEnabled_ = true;
            environmentIsEXR_ = true;
            std::free(exrPixels);
            return true;
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            error = L"Could not create WIC factory.";
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not decode environment image: " + path.wstring();
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) return false;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            error = L"Could not convert environment image to RGBA.";
            return false;
        }

        const UINT stride = w * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateTextureSRV(pixels.data(), w, h, srv, error)) return false;

        double sumR = 0.0, sumG = 0.0, sumB = 0.0, totalWeight = 0.0;
        double bestLum = -1.0;
        std::array<float, 3> brightest{1.0f, 1.0f, 1.0f};
        for (UINT y = 0; y < h; ++y)
        {
            const double theta = (static_cast<double>(y) + 0.5) / static_cast<double>(h) * 3.14159265358979323846;
            const double rowWeight = std::max(0.001, std::sin(theta));
            for (UINT x = 0; x < w; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                const double r = pixels[i + 0] / 255.0;
                const double g = pixels[i + 1] / 255.0;
                const double b = pixels[i + 2] / 255.0;
                sumR += r * rowWeight; sumG += g * rowWeight; sumB += b * rowWeight;
                totalWeight += rowWeight;
                const double lum = r * 0.2126 + g * 0.7152 + b * 0.0722;
                if (lum > bestLum)
                {
                    bestLum = lum;
                    brightest = {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
                }
            }
        }
        if (totalWeight > 0.0)
        {
            environmentAverageColor_ = {
                static_cast<float>(sumR / totalWeight),
                static_cast<float>(sumG / totalWeight),
                static_cast<float>(sumB / totalWeight)
            };
        }
        environmentSunColor_ = brightest;
        environmentSRV_ = srv;
        environmentPath_ = path;
        environmentEnabled_ = true;
        environmentIsEXR_ = false;
        return true;
    }

    bool LoadEnvironmentCubemapFaces(const std::array<fs::path, 6>& faces, std::wstring& error,
                                     UINT outputWidth, UINT outputHeight)
    {
        // Face order follows BO3's conventional names used by the APE sky assets:
        // +X right, -X left, +Y up, -Y down, +Z front, -Z back.
        struct FloatFace
        {
            UINT w = 0;
            UINT h = 0;
            std::vector<float> rgba;
        };
        std::array<FloatFace, 6> loaded{};

        auto bilinear = [](const FloatFace& face, float u, float v) -> std::array<float,4>
        {
            if (face.rgba.empty() || face.w == 0 || face.h == 0) return {0,0,0,1};
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            const float fx = u * static_cast<float>(face.w - 1);
            const float fy = v * static_cast<float>(face.h - 1);
            const UINT x0 = static_cast<UINT>(fx);
            const UINT y0 = static_cast<UINT>(fy);
            const UINT x1 = std::min(face.w - 1, x0 + 1);
            const UINT y1 = std::min(face.h - 1, y0 + 1);
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            std::array<float,4> out{};
            for (int c = 0; c < 4; ++c)
            {
                const float a = face.rgba[(static_cast<size_t>(y0) * face.w + x0) * 4 + c];
                const float b = face.rgba[(static_cast<size_t>(y0) * face.w + x1) * 4 + c];
                const float d = face.rgba[(static_cast<size_t>(y1) * face.w + x0) * 4 + c];
                const float e = face.rgba[(static_cast<size_t>(y1) * face.w + x1) * 4 + c];
                const float top = a + (b - a) * tx;
                const float bottom = d + (e - d) * tx;
                out[c] = top + (bottom - top) * ty;
            }
            return out;
        };

        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            float* exrPixels = nullptr;
            int sourceW = 0, sourceH = 0;
            const char* exrError = nullptr;
            const std::string utf8Path = WideToUtf8(faces[faceIndex].wstring());
            const int result = LoadEXR(&exrPixels, &sourceW, &sourceH, utf8Path.c_str(), &exrError);
            if (result != TINYEXR_SUCCESS || !exrPixels || sourceW <= 0 || sourceH <= 0)
            {
                if (exrError)
                {
                    error = L"Could not decode APE cubemap face '" + faces[faceIndex].wstring() + L"': " + Utf8ToWide(exrError);
                    FreeEXRErrorMessage(exrError);
                }
                else
                    error = L"Could not decode APE cubemap face: " + faces[faceIndex].wstring();
                if (exrPixels) std::free(exrPixels);
                return false;
            }

            const UINT srcW = static_cast<UINT>(sourceW);
            const UINT srcH = static_cast<UINT>(sourceH);
            const UINT maxFaceDimension = 1024;
            const float reduction = std::min(1.0f, static_cast<float>(maxFaceDimension) /
                static_cast<float>(std::max(srcW, srcH)));
            FloatFace face;
            face.w = std::max<UINT>(1u, static_cast<UINT>(std::lround(srcW * reduction)));
            face.h = std::max<UINT>(1u, static_cast<UINT>(std::lround(srcH * reduction)));
            face.rgba.resize(static_cast<size_t>(face.w) * face.h * 4u);

            if (face.w == srcW && face.h == srcH)
            {
                std::copy(exrPixels, exrPixels + static_cast<size_t>(srcW) * srcH * 4u, face.rgba.begin());
            }
            else
            {
                FloatFace sourceFace;
                sourceFace.w = srcW;
                sourceFace.h = srcH;
                sourceFace.rgba.assign(exrPixels, exrPixels + static_cast<size_t>(srcW) * srcH * 4u);
                for (UINT y = 0; y < face.h; ++y)
                {
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(face.h);
                    for (UINT x = 0; x < face.w; ++x)
                    {
                        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(face.w);
                        const auto sample = bilinear(sourceFace, u, v);
                        const size_t dst = (static_cast<size_t>(y) * face.w + x) * 4u;
                        for (int c = 0; c < 4; ++c) face.rgba[dst + c] = sample[c];
                    }
                }
            }
            std::free(exrPixels);
            loaded[faceIndex] = std::move(face);
        }

        outputWidth = std::clamp<UINT>(outputWidth, 256u, 4096u);
        outputHeight = std::clamp<UINT>(outputHeight, 128u, 2048u);
        if (outputWidth != outputHeight * 2u) outputWidth = outputHeight * 2u;
        std::vector<float> equirect(static_cast<size_t>(outputWidth) * outputHeight * 4u, 0.0f);

        constexpr float kPi = 3.14159265358979323846f;
        double sumR = 0.0, sumG = 0.0, sumB = 0.0, totalWeight = 0.0;
        double bestLum = -1.0;
        std::array<float,3> brightest{1,1,1};

        auto sampleCube = [&](float x, float y, float z) -> std::array<float,4>
        {
            const float ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
            int face = 0;
            float s = 0.0f, t = 0.0f, major = 1.0f;
            if (ax >= ay && ax >= az)
            {
                major = std::max(ax, 1e-8f);
                if (x >= 0.0f) { face = 0; s = -z / major; t = -y / major; }
                else           { face = 1; s =  z / major; t = -y / major; }
            }
            else if (ay >= ax && ay >= az)
            {
                major = std::max(ay, 1e-8f);
                if (y >= 0.0f) { face = 2; s =  x / major; t =  z / major; }
                else           { face = 3; s =  x / major; t = -z / major; }
            }
            else
            {
                major = std::max(az, 1e-8f);
                if (z >= 0.0f) { face = 4; s =  x / major; t = -y / major; }
                else           { face = 5; s = -x / major; t = -y / major; }
            }
            return bilinear(loaded[face], s * 0.5f + 0.5f, t * 0.5f + 0.5f);
        };

        for (UINT y = 0; y < outputHeight; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputHeight);
            const float theta = v * kPi;
            const float sinTheta = std::sin(theta);
            const double rowWeight = std::max(0.001, static_cast<double>(sinTheta));
            for (UINT x = 0; x < outputWidth; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputWidth);
                const float phi = (u - 0.5f) * (2.0f * kPi);
                const float dx = sinTheta * std::cos(phi);
                const float dy = std::cos(theta);
                const float dz = sinTheta * std::sin(phi);
                auto sample = sampleCube(dx, dy, dz);
                sample[0] = std::max(0.0f, sample[0]);
                sample[1] = std::max(0.0f, sample[1]);
                sample[2] = std::max(0.0f, sample[2]);
                sample[3] = std::clamp(sample[3], 0.0f, 1.0f);
                const size_t dst = (static_cast<size_t>(y) * outputWidth + x) * 4u;
                for (int c = 0; c < 4; ++c) equirect[dst + c] = sample[c];

                sumR += static_cast<double>(sample[0]) * rowWeight;
                sumG += static_cast<double>(sample[1]) * rowWeight;
                sumB += static_cast<double>(sample[2]) * rowWeight;
                totalWeight += rowWeight;
                const double lum = sample[0] * 0.2126 + sample[1] * 0.7152 + sample[2] * 0.0722;
                if (lum > bestLum) { bestLum = lum; brightest = {sample[0], sample[1], sample[2]}; }
            }
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateFloatTextureSRV(equirect.data(), outputWidth, outputHeight, srv, error)) return false;
        if (totalWeight > 0.0)
        {
            environmentAverageColor_ = {
                static_cast<float>(sumR / totalWeight),
                static_cast<float>(sumG / totalWeight),
                static_cast<float>(sumB / totalWeight)
            };
        }
        const float maxSun = std::max({brightest[0], brightest[1], brightest[2], 0.0001f});
        environmentSunColor_ = {
            std::clamp(brightest[0] / maxSun, 0.0f, 1.0f),
            std::clamp(brightest[1] / maxSun, 0.0f, 1.0f),
            std::clamp(brightest[2] / maxSun, 0.0f, 1.0f)
        };
        environmentSRV_ = srv;
        environmentPath_ = faces[4]; // front face is the most useful display name
        environmentEnabled_ = true;
        environmentIsEXR_ = true;
        return true;
    }

    bool CreateDefaultStudioEnvironment(std::wstring& error)
    {
        const UINT w = 1024;
        const UINT h = 512;
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4u, 255u);

        auto lerp3 = [](const std::array<float,3>& a, const std::array<float,3>& b, float t) -> std::array<float,3>
        {
            return {
                a[0] + (b[0] - a[0]) * t,
                a[1] + (b[1] - a[1]) * t,
                a[2] + (b[2] - a[2]) * t
            };
        };
        const std::array<float,3> zenith{0.34f, 0.51f, 0.76f};
        const std::array<float,3> horizon{0.86f, 0.89f, 0.94f};
        const std::array<float,3> groundA{0.070f, 0.074f, 0.082f};
        const std::array<float,3> groundB{0.11f, 0.115f, 0.125f};
        const float sunYaw = DirectX::XMConvertToRadians(135.0f);
        const float sunPitch = DirectX::XMConvertToRadians(45.0f);
        const std::array<float,3> sunDir{
            std::cos(sunPitch) * std::cos(sunYaw),
            std::sin(sunPitch),
            std::cos(sunPitch) * std::sin(sunYaw)
        };

        for (UINT y = 0; y < h; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
            const float phi = v * 3.1415926535f;
            const float dirY = std::cos(phi);
            const float pr = std::sin(phi);
            for (UINT x = 0; x < w; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
                const float theta = u * 6.283185307f;
                const std::array<float,3> dir{std::cos(theta) * pr, dirY, std::sin(theta) * pr};
                std::array<float,3> c{};
                if (dirY >= 0.0f)
                {
                    const float tSky = std::pow(std::clamp(dirY, 0.0f, 1.0f), 0.6f);
                    c = lerp3(horizon, zenith, tSky);
                    const float horizonGlow = std::pow(1.0f - std::clamp(dirY, 0.0f, 1.0f), 3.0f);
                    c[0] += 0.035f * horizonGlow;
                    c[1] += 0.025f * horizonGlow;
                    c[2] += 0.015f * horizonGlow;
                    const float cloud1 = 0.5f + 0.5f * std::sin(theta * 3.0f + phi * 9.0f + std::sin(theta * 5.0f) * 0.7f);
                    const float cloud2 = 0.5f + 0.5f * std::sin(theta * 11.0f - phi * 17.0f + 0.5f);
                    const float cloudMix = std::pow(std::clamp(cloud1 * 0.6f + cloud2 * 0.4f, 0.0f, 1.0f), 3.5f);
                    const float cloudMask = cloudMix * std::pow(1.0f - std::clamp(dirY, 0.0f, 1.0f), 0.45f) * 0.22f;
                    c = lerp3(c, {0.98f, 0.985f, 0.99f}, cloudMask);
                    const float sunDot = std::clamp(dir[0] * sunDir[0] + dir[1] * sunDir[1] + dir[2] * sunDir[2], 0.0f, 1.0f);
                    const float sunGlow = std::pow(sunDot, 64.0f) * 1.35f + std::pow(sunDot, 512.0f) * 4.5f;
                    c[0] += sunGlow * 1.0f;
                    c[1] += sunGlow * 0.96f;
                    c[2] += sunGlow * 0.88f;
                }
                else
                {
                    const float tGround = std::pow(std::clamp(-dirY, 0.0f, 1.0f), 0.6f);
                    c = lerp3(groundB, groundA, tGround);
                }
                const size_t i = (static_cast<size_t>(y) * w + x) * 4u;
                pixels[i + 0] = static_cast<uint8_t>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[i + 1] = static_cast<uint8_t>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[i + 2] = static_cast<uint8_t>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[i + 3] = 255u;
            }
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateTextureSRV(pixels.data(), w, h, srv, error))
            return false;
        environmentSRV_ = srv;
        environmentPath_ = fs::path(L"<Built-in Studio Sky>");
        environmentAverageColor_ = {0.72f, 0.77f, 0.84f};
        environmentSunColor_ = {1.0f, 0.97f, 0.92f};
        environmentEnabled_ = true;
        environmentIsEXR_ = false;
        return true;
    }

    void ClearEnvironmentTexture()
    {
        environmentSRV_.Reset();
        environmentPath_.clear();
        environmentAverageColor_ = {0.18f, 0.18f, 0.18f};
        environmentSunColor_ = {1.0f, 1.0f, 1.0f};
        environmentEnabled_ = false;
        environmentIsEXR_ = false;
        std::wstring error;
        CreateDefaultStudioEnvironment(error);
    }

    bool EnvironmentEnabled() const { return environmentEnabled_ && environmentSRV_.Get() != nullptr; }
    bool EnvironmentIsEXR() const { return environmentIsEXR_; }
    std::wstring EnvironmentPath() const { return environmentPath_.wstring(); }

    bool BakeCurrentSkyToEXR(const fs::path& outputPath, int outputWidth, int outputHeight, std::wstring& error)
    {
        if (!device_ || !context_ || !pixelShader_ || !skyVertexShader_)
        {
            error = L"The Direct3D sky preview is not initialized.";
            return false;
        }
        if (!skyShaderMode_ && previewMode_ != PreviewMode::Sky)
        {
            error = L"The current shader is not detected as a directional sky shader.";
            return false;
        }
        outputWidth = std::max(256, outputWidth);
        outputHeight = std::max(128, outputHeight);
        const UINT faceSize = static_cast<UINT>(std::clamp(outputWidth / 4, 256, 2048));

        D3D11_TEXTURE2D_DESC renderDesc{};
        renderDesc.Width = faceSize;
        renderDesc.Height = faceSize;
        renderDesc.MipLevels = 1;
        renderDesc.ArraySize = 1;
        renderDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        renderDesc.SampleDesc.Count = 1;
        renderDesc.Usage = D3D11_USAGE_DEFAULT;
        renderDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

        ComPtr<ID3D11Texture2D> faceTexture;
        ComPtr<ID3D11RenderTargetView> faceRTV;
        if (FAILED(device_->CreateTexture2D(&renderDesc, nullptr, faceTexture.GetAddressOf())) ||
            FAILED(device_->CreateRenderTargetView(faceTexture.Get(), nullptr, faceRTV.GetAddressOf())))
        {
            error = L"Could not create the HDR sky bake render target.";
            return false;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = renderDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
        {
            error = L"Could not create the HDR sky bake staging texture.";
            return false;
        }

        const float oldYaw = cameraYawDegrees_;
        const float oldPitch = cameraPitchDegrees_;
        const float oldFov = cameraFovDegrees_;
        const UINT oldSourceW = sourceWidth_;
        const UINT oldSourceH = sourceHeight_;
        sourceWidth_ = faceSize;
        sourceHeight_ = faceSize;
        cameraFovDegrees_ = 90.0f;

        struct FaceAngle { float yaw; float pitch; };
        const std::array<FaceAngle, 6> angles{{
            {0.0f, 0.0f}, {180.0f, 0.0f}, {90.0f, 0.0f}, {-90.0f, 0.0f}, {0.0f, 90.0f}, {0.0f, -90.0f}
        }};
        std::array<std::vector<float>, 6> faces;
        bool ok = true;

        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(faceSize);
        vp.Height = static_cast<float>(faceSize);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        const float clear[4] = {0,0,0,1};

        for (size_t faceIndex = 0; faceIndex < angles.size() && ok; ++faceIndex)
        {
            cameraYawDegrees_ = angles[faceIndex].yaw;
            cameraPitchDegrees_ = angles[faceIndex].pitch;
            context_->ClearRenderTargetView(faceRTV.Get(), clear);
            context_->OMSetRenderTargets(1, faceRTV.GetAddressOf(), nullptr);
            context_->RSSetViewports(1, &vp);
            context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(skyVertexShader_.Get(), nullptr, 0);
            // Cubemap export deliberately renders six authored camera faces; a
            // preview-only movement lock must never flatten the exported sky.
            UpdateCameraVertexBuffer(vp, false);
            context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
            BindPostFxResources();
            UpdateConstantBuffers(vp);
            context_->Draw(3, 0);
            UnbindAllPixelResources();
            context_->CopyResource(staging.Get(), faceTexture.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                error = L"Could not read back an HDR sky cube face.";
                ok = false;
                break;
            }
            auto& dst = faces[faceIndex];
            dst.resize(static_cast<size_t>(faceSize) * faceSize * 4u);
            for (UINT y = 0; y < faceSize; ++y)
            {
                const auto* src = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch);
                std::memcpy(dst.data() + static_cast<size_t>(y) * faceSize * 4u, src, static_cast<size_t>(faceSize) * 4u * sizeof(float));
            }
            context_->Unmap(staging.Get(), 0);
        }

        cameraYawDegrees_ = oldYaw;
        cameraPitchDegrees_ = oldPitch;
        cameraFovDegrees_ = oldFov;
        sourceWidth_ = oldSourceW;
        sourceHeight_ = oldSourceH;
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        if (!ok) return false;

        auto sampleFace = [&](int face, float ndcX, float ndcY, int channel) -> float
        {
            const auto& f = faces[static_cast<size_t>(face)];
            const float u = std::clamp((ndcX + 1.0f) * 0.5f, 0.0f, 1.0f) * static_cast<float>(faceSize - 1);
            const float v = std::clamp((1.0f - ndcY) * 0.5f, 0.0f, 1.0f) * static_cast<float>(faceSize - 1);
            const int x0 = static_cast<int>(std::floor(u));
            const int y0 = static_cast<int>(std::floor(v));
            const int x1 = std::min<int>(x0 + 1, static_cast<int>(faceSize - 1));
            const int y1 = std::min<int>(y0 + 1, static_cast<int>(faceSize - 1));
            const float tx = u - x0;
            const float ty = v - y0;
            auto at = [&](int x, int y) { return f[(static_cast<size_t>(y) * faceSize + static_cast<size_t>(x)) * 4u + static_cast<size_t>(channel)]; };
            const float a = at(x0,y0) * (1.0f-tx) + at(x1,y0) * tx;
            const float b = at(x0,y1) * (1.0f-tx) + at(x1,y1) * tx;
            return a * (1.0f-ty) + b * ty;
        };

        std::vector<float> latlong(static_cast<size_t>(outputWidth) * outputHeight * 4u);
        constexpr float pi = 3.14159265358979323846f;
        for (int y = 0; y < outputHeight; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outputHeight);
            const float latitude = v * pi;
            const float sinLat = std::sin(latitude);
            const float z = std::cos(latitude);
            for (int x = 0; x < outputWidth; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outputWidth);
                const float longitude = (0.5f - u) * (2.0f * pi);
                const float dx = sinLat * std::cos(longitude);
                const float dy = sinLat * std::sin(longitude);
                const float dz = z;
                const float ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
                int face = 0;
                float nx = 0.0f, ny = 0.0f;
                if (ax >= ay && ax >= az)
                {
                    if (dx >= 0.0f) { face = 0; nx = dy / ax; ny = dz / ax; }
                    else { face = 1; nx = -dy / ax; ny = dz / ax; }
                }
                else if (ay >= ax && ay >= az)
                {
                    if (dy >= 0.0f) { face = 2; nx = -dx / ay; ny = dz / ay; }
                    else { face = 3; nx = dx / ay; ny = dz / ay; }
                }
                else
                {
                    if (dz >= 0.0f) { face = 4; nx = dy / az; ny = -dx / az; }
                    else { face = 5; nx = dy / az; ny = dx / az; }
                }
                const size_t base = (static_cast<size_t>(y) * outputWidth + static_cast<size_t>(x)) * 4u;
                latlong[base + 0] = sampleFace(face, nx, ny, 0);
                latlong[base + 1] = sampleFace(face, nx, ny, 1);
                latlong[base + 2] = sampleFace(face, nx, ny, 2);
                latlong[base + 3] = 1.0f;
            }
        }

        std::error_code ec;
        fs::create_directories(outputPath.parent_path(), ec);
        const std::string utf8Path = WideToUtf8(outputPath.wstring());
        const char* exrErr = nullptr;
        const int saveResult = SaveEXR(latlong.data(), outputWidth, outputHeight, 4, 1, utf8Path.c_str(), &exrErr);
        if (saveResult != TINYEXR_SUCCESS)
        {
            error = exrErr ? (L"Could not save baked EXR: " + Utf8ToWide(exrErr)) : L"Could not save baked EXR.";
            if (exrErr) FreeEXRErrorMessage(exrErr);
            return false;
        }
        return true;
    }
    void SetEnvironmentAffectsLighting(bool enabled) { environmentAffectsLighting_ = enabled; }
    bool EnvironmentAffectsLighting() const { return environmentAffectsLighting_; }
    void SetFulbright(bool enabled) { fulbright_ = enabled; }
    bool Fulbright() const { return fulbright_; }
    void SetMaterialPreviewProfile(MaterialPreviewProfile profile) { materialPreviewProfile_ = profile; }
    MaterialPreviewProfile GetMaterialPreviewProfile() const { return materialPreviewProfile_; }
    void SetLightColor(float r, float g, float b)
    {
        lightColor_ = {std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b)};
        useExplicitLightColor_ = true;
    }
    std::array<float,3> LightColor() const
    {
        return useExplicitLightColor_ ? lightColor_ : environmentSunColor_;
    }
    void ResetLightColorToEnvironment() { useExplicitLightColor_ = false; }
    void SetEnvironmentRotationDegrees(float degrees)
    {
        environmentRotationDegrees_ = std::fmod(degrees, 360.0f);
        if (environmentRotationDegrees_ < 0.0f) environmentRotationDegrees_ += 360.0f;
    }
    float EnvironmentRotationDegrees() const { return environmentRotationDegrees_; }

    bool LoadShadertoyChannelTexture(int channel, const fs::path& path, bool flipY, std::wstring& error)
    {
        if (channel < 0 || channel >= kShadertoyChannelCount)
        {
            error = L"Invalid Shadertoy channel.";
            return false;
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            error = L"Could not create WIC factory.";
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not decode image: " + path.wstring();
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) return false;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0)
        {
            error = L"Image has invalid dimensions.";
            return false;
        }

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            error = L"Could not convert image to RGBA.";
            return false;
        }

        const UINT stride = w * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        if (flipY && h > 1)
        {
            std::vector<uint8_t> row(static_cast<size_t>(stride));
            for (UINT y = 0; y < h / 2; ++y)
            {
                uint8_t* top = pixels.data() + static_cast<size_t>(y) * stride;
                uint8_t* bottom = pixels.data() + static_cast<size_t>(h - 1 - y) * stride;
                std::memcpy(row.data(), top, stride);
                std::memcpy(top, bottom, stride);
                std::memcpy(bottom, row.data(), stride);
            }
        }

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateMipmappedTextureSRV(pixels.data(), w, h, srv, error)) return false;

        shadertoyChannelSRVs_[static_cast<size_t>(channel)] = srv;
        shadertoyChannelPaths_[static_cast<size_t>(channel)] = path;
        shadertoyChannelWidths_[static_cast<size_t>(channel)] = w;
        shadertoyChannelHeights_[static_cast<size_t>(channel)] = h;
        shadertoyChannelFlipY_[static_cast<size_t>(channel)] = flipY;
        return true;
    }

    void ClearShadertoyChannelTexture(int channel)
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return;
        shadertoyChannelSRVs_[static_cast<size_t>(channel)].Reset();
        shadertoyChannelPaths_[static_cast<size_t>(channel)].clear();
        shadertoyChannelWidths_[static_cast<size_t>(channel)] = 0;
        shadertoyChannelHeights_[static_cast<size_t>(channel)] = 0;
    }

    void SetShadertoyChannelRepeat(int channel, bool repeat)
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return;
        shadertoyChannelRepeat_[static_cast<size_t>(channel)] = repeat;
    }

    bool GetShadertoyChannelRepeat(int channel) const
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return true;
        return shadertoyChannelRepeat_[static_cast<size_t>(channel)];
    }

    bool GetShadertoyChannelFlipY(int channel) const
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return true;
        return shadertoyChannelFlipY_[static_cast<size_t>(channel)];
    }

    std::wstring GetShadertoyChannelPath(int channel) const
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return L"";
        return shadertoyChannelPaths_[static_cast<size_t>(channel)].wstring();
    }

    UINT GetShadertoyChannelWidth(int channel) const
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return 0u;
        return shadertoyChannelWidths_[static_cast<size_t>(channel)];
    }

    UINT GetShadertoyChannelHeight(int channel) const
    {
        if (channel < 0 || channel >= kShadertoyChannelCount) return 0u;
        return shadertoyChannelHeights_[static_cast<size_t>(channel)];
    }

    std::vector<int> RequiredShadertoyChannels() const
    {
        std::array<bool, kShadertoyChannelCount> required{{false,false,false,false}};
        for (const auto& resource : resources_)
        {
            if (resource.name.rfind("iChannel", 0) != 0 || resource.name.size() != 9) continue;
            const char digit = resource.name[8];
            if (digit >= '0' && digit <= '3')
                required[static_cast<size_t>(digit - '0')] = true;
        }
        std::vector<int> result;
        for (int channel = 0; channel < kShadertoyChannelCount; ++channel)
            if (required[static_cast<size_t>(channel)]) result.push_back(channel);
        return result;
    }

    std::vector<int> MissingShadertoyChannels() const
    {
        std::vector<int> result;
        for (const int channel : RequiredShadertoyChannels())
        {
            // A strict live package can deliberately replace an iChannel with
            // an engine input such as resolvedScene/floatZ. In that case a
            // separately loaded Shadertoy image is no longer required and the
            // old warning was both noisy and misleading. Material-image
            // mappings still require an actual local/user texture.
            const std::string resourceName = "iChannel" + std::to_string(channel);
            const auto mappedRole = previewResourceRoles_.find(resourceName);
            if(mappedRole != previewResourceRoles_.end() &&
               (mappedRole->second == bo3::PackageResourceRole::ResolvedScene ||
                mappedRole->second == bo3::PackageResourceRole::FloatDepth ||
                mappedRole->second == bo3::PackageResourceRole::Ignore))
                continue;

            if (!shadertoyChannelSRVs_[static_cast<size_t>(channel)])
                result.push_back(channel);
        }
        return result;
    }

    bool LoadMaterialTexture(int logicalSlot, const fs::path& path, std::wstring& error)
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount)
        {
            error = L"Invalid material texture slot.";
            return false;
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            error = L"Could not create WIC factory.";
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not decode image: " + path.wstring();
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) return false;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            error = L"Could not convert image to RGBA.";
            return false;
        }

        const UINT stride = w * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        // BO3 image semantics are not all linear. APE converts diffuse/effect
        // color maps (and specular color maps) through an sRGB resource view,
        // while normals/gloss/AO/height remain linear. Material Textures uses a
        // fixed logical contract, so preserve that distinction in the preview.
        const bool srgb = logicalSlot == 0 || logicalSlot == 3 || logicalSlot == 6;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateMipmappedTextureSRV(pixels.data(), w, h, srv, error, srgb)) return false;

        materialTextureSRVs_[static_cast<size_t>(logicalSlot)] = srv;
        materialTexturePaths_[static_cast<size_t>(logicalSlot)] = path;
        materialTextureWidths_[static_cast<size_t>(logicalSlot)] = w;
        materialTextureHeights_[static_cast<size_t>(logicalSlot)] = h;
        materialTextureSrgb_[static_cast<size_t>(logicalSlot)] = srgb;
        return true;
    }

    void ClearMaterialTexture(int logicalSlot)
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return;
        materialTextureSRVs_[static_cast<size_t>(logicalSlot)].Reset();
        materialTexturePaths_[static_cast<size_t>(logicalSlot)].clear();
        materialTextureWidths_[static_cast<size_t>(logicalSlot)] = 0;
        materialTextureHeights_[static_cast<size_t>(logicalSlot)] = 0;
        materialTextureSrgb_[static_cast<size_t>(logicalSlot)] = false;
    }

    void SetMaterialTextureBinding(int logicalSlot, UINT bindSlot)
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return;
        materialTextureBindings_[static_cast<size_t>(logicalSlot)] = bindSlot;
    }

    UINT GetMaterialTextureBinding(int logicalSlot) const
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return 0u;
        return materialTextureBindings_[static_cast<size_t>(logicalSlot)];
    }

    std::wstring GetMaterialTexturePath(int logicalSlot) const
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return L"";
        return materialTexturePaths_[static_cast<size_t>(logicalSlot)].wstring();
    }

    UINT GetMaterialTextureWidth(int logicalSlot) const
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return 0;
        return materialTextureWidths_[static_cast<size_t>(logicalSlot)];
    }

    UINT GetMaterialTextureHeight(int logicalSlot) const
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return 0;
        return materialTextureHeights_[static_cast<size_t>(logicalSlot)];
    }

    bool GetMaterialTextureIsSrgb(int logicalSlot) const
    {
        if (logicalSlot < 0 || logicalSlot >= kMaterialTextureSlotCount) return false;
        return materialTextureSrgb_[static_cast<size_t>(logicalSlot)];
    }

    void SetMaterialUvScale(float u, float v)
    {
        materialUvScaleU_ = std::clamp(u, 0.01f, 100.0f);
        materialUvScaleV_ = std::clamp(v, 0.01f, 100.0f);
    }
    float MaterialUvScaleU() const { return materialUvScaleU_; }
    float MaterialUvScaleV() const { return materialUvScaleV_; }
    void ResetMaterialUvScale() { materialUvScaleU_ = 1.0f; materialUvScaleV_ = 1.0f; }

    void ResetTemporalExposureHistory() { ResetTemporalExposureState(); }
    bool TemporalExposureActive() const { return temporalExposureMode_ && temporalExposureStatePS_.Get() != nullptr; }

    bool StartLiveCapture(HWND target, std::wstring& error)
    {
        QString captureError;
        if(!liveWindowCapture_.start(target, device_.Get(), captureError))
        {
            error = captureError.toStdWString();
            return false;
        }
        UnbindAllPixelResources();
        sourceSRV_.Reset();
        sourceWidth_ = sourceHeight_ = 0;
        sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
        depthUserLoaded_ = false;
        builtInDepthScene_ = false;
        capturedBO3DepthScene_ = false;
        previewZNear_ = 0.1f;
        liveCaptureSource_ = true;
        liveCaptureNotification_.clear();
        ResetTemporalExposureState();
        return true;
    }

    void StopLiveCapture()
    {
        liveWindowCapture_.stop();
        if(liveCaptureSource_)
        {
            UnbindAllPixelResources();
            sourceSRV_.Reset();
            sourceWidth_ = sourceHeight_ = 0;
            runtimeSceneSRV_.Reset();
            runtimeSceneRTV_.Reset();
            runtimeSceneTexture_.Reset();
        }
        liveCaptureSource_ = false;
        builtInDepthScene_ = false;
        capturedBO3DepthScene_ = false;
        previewZNear_ = 0.1f;
        liveCaptureNotification_.clear();
        ResetTemporalExposureState();
    }

    bool LiveCaptureActive() const { return liveWindowCapture_.active(); }
    bool IsLiveCaptureSource() const { return liveCaptureSource_; }
    bo3::LiveCaptureDiagnostics LiveCaptureInfo() const { return liveWindowCapture_.diagnostics(); }
    const QString& LiveCaptureNotification() const { return liveCaptureNotification_; }
    void SetLiveComparisonMode(LiveComparisonMode mode) { liveComparisonMode_ = mode; }
    LiveComparisonMode GetLiveComparisonMode() const { return liveComparisonMode_; }
    void SetLiveSplitFraction(float value) { liveSplitFraction_ = std::clamp(value, 0.05f, 0.95f); }
    float LiveSplitFraction() const { return liveSplitFraction_; }

    bool LoadTexture(const fs::path& path, bool depth, std::wstring& error)
    {
        if(!depth && liveCaptureSource_) StopLiveCapture();
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

        // PostFX t0 can now be a real scene-linear floating-point source. This
        // is the accurate BO3 Runtime path: values above 1.0 survive and are
        // passed into the resolvedScene simulation without an LDR round-trip.
        if (!depth && extension == ".exr")
        {
            float* exrPixels = nullptr;
            int exrWidth = 0;
            int exrHeight = 0;
            const char* exrError = nullptr;
            const std::string utf8Path = WideToUtf8(path.wstring());
            const int result = LoadEXR(&exrPixels, &exrWidth, &exrHeight, utf8Path.c_str(), &exrError);
            if (result != TINYEXR_SUCCESS || !exrPixels || exrWidth <= 0 || exrHeight <= 0)
            {
                if (exrError)
                {
                    error = L"Could not decode EXR source image: " + Utf8ToWide(exrError);
                    FreeEXRErrorMessage(exrError);
                }
                else
                {
                    error = L"Could not decode EXR source image: " + path.wstring();
                }
                if (exrPixels) std::free(exrPixels);
                return false;
            }

            const UINT w = static_cast<UINT>(exrWidth);
            const UINT h = static_cast<UINT>(exrHeight);
            double sumLum = 0.0;
            double peakLum = 0.0;
            uint64_t lumCount = 0;
            for (size_t i = 0, count = static_cast<size_t>(w) * h; i < count; ++i)
            {
                float* px = exrPixels + i * 4;
                for (int c = 0; c < 3; ++c)
                    if (!std::isfinite(px[c]) || px[c] < 0.0f) px[c] = 0.0f;
                if (!std::isfinite(px[3])) px[3] = 1.0f;
                px[3] = std::clamp(px[3], 0.0f, 1.0f);
                const double lum = static_cast<double>(px[0]) * 0.2126 +
                                   static_cast<double>(px[1]) * 0.7152 +
                                   static_cast<double>(px[2]) * 0.0722;
                peakLum = std::max(peakLum, lum);
                sumLum += lum;
                ++lumCount;
            }

            ComPtr<ID3D11ShaderResourceView> srv;
            const bool uploaded = CreateFloatTextureSRV(exrPixels, w, h, srv, error);
            std::free(exrPixels);
            if (!uploaded) return false;

            depthUserLoaded_ = false;
            builtInDepthScene_ = false;
            capturedBO3DepthScene_ = false;
            previewZNear_ = 0.1f;
            sourceSRV_ = srv;
            sourceWidth_ = w;
            sourceHeight_ = h;
            sourceEncoding_ = PreviewSourceEncoding::HdrSceneLinear;
            sourceHdrPeakLuminance_ = static_cast<float>(std::max(0.0, peakLum));
            sourceHdrMeanLuminance_ = lumCount > 0
                ? static_cast<float>(sumLum / static_cast<double>(lumCount)) : 0.0f;
            return true;
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr))
        {
            error = L"Could not create WIC factory.";
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not decode image: " + path.wstring();
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.GetAddressOf());
        if (FAILED(hr)) return false;

        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0) return false;

        ComPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) return false;
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr))
        {
            error = L"Could not convert image to RGBA.";
            return false;
        }

        const UINT stride = w * 4;
        std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
        hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
        if (FAILED(hr)) return false;

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!CreateTextureSRV(pixels.data(), w, h, srv, error)) return false;

        if (depth)
        {
            depthSRV_ = srv;
            depthWidth_ = w;
            depthHeight_ = h;
            depthUserLoaded_ = true;
            builtInDepthScene_ = false;
            capturedBO3DepthScene_ = false;
            previewZNear_ = 0.1f;
        }
        else
        {
            // A depth map only matches the image it was authored/captured with.
            // Loading a new source invalidates the previous preview depth rather
            // than silently reusing it and producing false diagonal/contact artifacts.
            depthUserLoaded_ = false;
            builtInDepthScene_ = false;
            capturedBO3DepthScene_ = false;
            previewZNear_ = 0.1f;
            sourceSRV_ = srv;
            sourceWidth_ = w;
            sourceHeight_ = h;
            sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
            sourceHdrPeakLuminance_ = 1.0f;
            sourceHdrMeanLuminance_ = 0.18f;
        }
        return true;
    }

    bool DecodeBO3DepthCaptureSheet(QImage sheet, std::wstring& error, bool builtInScene)
    {
        if(liveCaptureSource_) StopLiveCapture();
        if(sheet.isNull())
        {
            error = L"Could not decode the bundled BO3 ground-truth Float-Z scene.";
            return false;
        }
        sheet = sheet.convertToFormat(QImage::Format_RGBA8888);
        if(sheet.width() < 640 || sheet.height() < 360 || (sheet.width() & 1) != 0 || (sheet.height() & 1) != 0)
        {
            error = L"BO3 Float-Z capture sheets must be an even-sized 2x2 capture image (minimum 640x360). Do not crop or resize the screenshot.";
            return false;
        }

        const int captureW = sheet.width() / 2;
        const int captureH = sheet.height() / 2;
        constexpr int kLevels = 32;
        constexpr float kDepthLogMin = -5.0f;   // 0.03125 world units/meters in the diagnostic convention.
        constexpr float kDepthLogMax = 17.0f;   // 131072.0
        constexpr float kZNearLogMin = -12.0f;
        constexpr float kZNearLogMax = 0.0f;
        constexpr float kDepthHackSplit = 63.0f / 64.0f;

        // The bottom-right quadrant contains a 32-level neutral calibration ramp.
        // The importer learns the screenshot/display transfer from the capture
        // itself rather than assuming the game/window capture is linear or sRGB.
        std::array<std::array<float, kLevels>, 3> calibration{};
        std::array<std::array<float, kLevels>, 3> calibrationStdDev{};
        for(int level = 0; level < kLevels; ++level)
        {
            const int localX0 = std::clamp(static_cast<int>(std::floor((level + 0.22f) * captureW / kLevels)), 0, captureW - 1);
            const int localX1 = std::clamp(static_cast<int>(std::ceil ((level + 0.78f) * captureW / kLevels)), localX0 + 1, captureW);
            const int localY0 = std::clamp(static_cast<int>(captureH * 0.10f), 0, captureH - 1);
            const int localY1 = std::clamp(static_cast<int>(captureH * 0.58f), localY0 + 1, captureH);

            std::array<double,3> sum{0.0,0.0,0.0};
            std::array<double,3> sumSq{0.0,0.0,0.0};
            uint64_t count = 0;
            const int stepX = std::max(1, (localX1 - localX0) / 12);
            const int stepY = std::max(1, (localY1 - localY0) / 10);
            for(int y = localY0; y < localY1; y += stepY)
            {
                const uchar* row = sheet.constScanLine(captureH + y);
                for(int x = localX0; x < localX1; x += stepX)
                {
                    const uchar* px = row + (captureW + x) * 4;
                    for(int c = 0; c < 3; ++c)
                    {
                        const double v = px[c];
                        sum[c] += v;
                        sumSq[c] += v * v;
                    }
                    ++count;
                }
            }
            if(count == 0)
            {
                error = L"BO3 Float-Z capture calibration could not be sampled.";
                return false;
            }
            for(int c = 0; c < 3; ++c)
            {
                const double mean = sum[c] / static_cast<double>(count);
                const double variance = std::max(0.0, sumSq[c] / static_cast<double>(count) - mean * mean);
                calibration[c][level] = static_cast<float>(mean);
                calibrationStdDev[c][level] = static_cast<float>(std::sqrt(variance));
            }
        }

        for(int c = 0; c < 3; ++c)
        {
            for(int level = 0; level < kLevels; ++level)
            {
                if(calibrationStdDev[c][level] > 8.0f)
                {
                    error = L"BO3 Float-Z capture calibration is noisy or compressed. Use a lossless PNG at the game's native screenshot resolution (no JPEG, resizing, or filtering).";
                    return false;
                }
                if(level > 0 && calibration[c][level] <= calibration[c][level - 1] + 0.35f)
                {
                    error = L"BO3 Float-Z capture calibration levels collapsed. Use a lossless PNG and disable any external screenshot color/filter processing.";
                    return false;
                }
            }
        }

        auto decodeLevel = [&](int capturedByte, int channel) -> int
        {
            int best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for(int level = 0; level < kLevels; ++level)
            {
                const float d = std::abs(static_cast<float>(capturedByte) - calibration[channel][level]);
                if(d < bestDistance)
                {
                    bestDistance = d;
                    best = level;
                }
            }
            return best;
        };

        auto decodeTripletAt = [&](float localX, float localY) -> std::array<int,3>
        {
            const int cx = captureW + std::clamp(static_cast<int>(localX * captureW), 0, captureW - 1);
            const int cy = captureH + std::clamp(static_cast<int>(localY * captureH), 0, captureH - 1);
            std::array<double,3> sum{0.0,0.0,0.0};
            int samples = 0;
            for(int oy = -2; oy <= 2; ++oy)
            {
                const int sy = std::clamp(cy + oy, captureH, sheet.height() - 1);
                const uchar* row = sheet.constScanLine(sy);
                for(int ox = -2; ox <= 2; ++ox)
                {
                    const int sx = std::clamp(cx + ox, captureW, sheet.width() - 1);
                    const uchar* px = row + sx * 4;
                    for(int c = 0; c < 3; ++c) sum[c] += px[c];
                    ++samples;
                }
            }
            return {
                decodeLevel(static_cast<int>(std::lround(sum[0] / samples)), 0),
                decodeLevel(static_cast<int>(std::lround(sum[1] / samples)), 1),
                decodeLevel(static_cast<int>(std::lround(sum[2] / samples)), 2)
            };
        };

        const auto magicA = decodeTripletAt(0.25f, 0.74f);
        const auto magicB = decodeTripletAt(0.75f, 0.74f);
        if(magicA != std::array<int,3>{3,27,11} || magicB != std::array<int,3>{29,5,23})
        {
            error = L"The bundled BO3 ground-truth Float-Z scene failed its calibration/magic-marker validation.";
            return false;
        }

        const auto versionMark = decodeTripletAt(0.50f, 0.965f);
        if(versionMark != std::array<int,3>{1,19,30})
        {
            error = L"Unsupported BO3 Float-Z capture-sheet version.";
            return false;
        }

        const auto zTriplet = decodeTripletAt(0.50f, 0.86f);
        const uint32_t zCode = static_cast<uint32_t>(zTriplet[0]) |
                               (static_cast<uint32_t>(zTriplet[1]) << 5u) |
                               (static_cast<uint32_t>(zTriplet[2] & 15) << 10u);
        const float zNorm = static_cast<float>(zCode) / 16383.0f;
        const float capturedZNear = std::exp2(kZNearLogMin + (kZNearLogMax - kZNearLogMin) * zNorm);
        if(!std::isfinite(capturedZNear) || capturedZNear < 0.0001f || capturedZNear > 1.0f)
        {
            error = L"BO3 Float-Z capture contains an invalid zNear calibration value.";
            return false;
        }

        std::vector<uint8_t> color(static_cast<size_t>(captureW) * captureH * 4u, 255u);
        std::vector<float> rawDepth(static_cast<size_t>(captureW) * captureH * 4u, 0.0f);
        uint64_t offGridSamples = 0;
        const uint64_t totalDepthSamples = static_cast<uint64_t>(captureW) * captureH * 3u;

        auto nearestLevelDistance = [&](int capturedByte, int channel, int decodedLevel) -> float
        {
            return std::abs(static_cast<float>(capturedByte) - calibration[channel][decodedLevel]);
        };

        for(int y = 0; y < captureH; ++y)
        {
            const uchar* colorRow = sheet.constScanLine(y);
            const uchar* encodedRow = sheet.constScanLine(y);
            for(int x = 0; x < captureW; ++x)
            {
                const uchar* srcColor = colorRow + x * 4;
                const uchar* encoded = encodedRow + (captureW + x) * 4;
                const int r5 = decodeLevel(encoded[0], 0);
                const int g5 = decodeLevel(encoded[1], 1);
                const int b5 = decodeLevel(encoded[2], 2);
                if(nearestLevelDistance(encoded[0], 0, r5) > 7.0f) ++offGridSamples;
                if(nearestLevelDistance(encoded[1], 1, g5) > 7.0f) ++offGridSamples;
                if(nearestLevelDistance(encoded[2], 2, b5) > 7.0f) ++offGridSamples;

                const bool viewmodel = (b5 & 16) != 0;
                const uint32_t depthCode = static_cast<uint32_t>(r5) |
                                           (static_cast<uint32_t>(g5) << 5u) |
                                           (static_cast<uint32_t>(b5 & 15) << 10u);
                const float depthNorm = static_cast<float>(depthCode) / 16383.0f;
                const float distance = std::exp2(kDepthLogMin + (kDepthLogMax - kDepthLogMin) * depthNorm);
                const float processed = std::clamp(capturedZNear / std::max(distance, 0.0000001f), 0.00000001f, 0.999999f);
                float raw = viewmodel
                    ? (processed + 63.0f) / 64.0f
                    : processed * kDepthHackSplit;
                if(viewmodel) raw = std::clamp(raw, kDepthHackSplit + 0.000001f, 0.999999f);
                else raw = std::clamp(raw, 0.00000001f, kDepthHackSplit - 0.000001f);

                const size_t i = (static_cast<size_t>(y) * captureW + x) * 4u;
                color[i + 0] = srcColor[0];
                color[i + 1] = srcColor[1];
                color[i + 2] = srcColor[2];
                color[i + 3] = 255u;
                rawDepth[i + 0] = raw;
                rawDepth[i + 1] = raw;
                rawDepth[i + 2] = raw;
                rawDepth[i + 3] = 1.0f;
            }
        }

        if(totalDepthSamples > 0 && static_cast<double>(offGridSamples) / static_cast<double>(totalDepthSamples) > 0.08)
        {
            error = L"Too much of the encoded Float-Z quadrant was altered. Hide HUD/overlays and capture a lossless native-resolution PNG without resizing or video compression.";
            return false;
        }

        ComPtr<ID3D11ShaderResourceView> colorSrv;
        ComPtr<ID3D11ShaderResourceView> depthSrv;
        if(!CreateTextureSRV(color.data(), static_cast<UINT>(captureW), static_cast<UINT>(captureH), colorSrv, error)) return false;
        if(!CreateFloatTextureSRV(rawDepth.data(), static_cast<UINT>(captureW), static_cast<UINT>(captureH), depthSrv, error)) return false;

        sourceSRV_ = colorSrv;
        depthSRV_ = depthSrv;
        sourceWidth_ = depthWidth_ = static_cast<UINT>(captureW);
        sourceHeight_ = depthHeight_ = static_cast<UINT>(captureH);
        sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
        sourceHdrPeakLuminance_ = 1.0f;
        sourceHdrMeanLuminance_ = 0.20f;
        depthUserLoaded_ = false;
        builtInDepthScene_ = builtInScene;
        capturedBO3DepthScene_ = true;
        previewZNear_ = capturedZNear;
        ResetTemporalExposureState();
        return true;
    }


    bool UseBuiltInDepthScene(std::wstring& error, const QString& requestedSceneId)
    {
        if(liveCaptureSource_) StopLiveCapture();

        struct DepthPreviewScene
        {
            const char* id;
            const char* colorResource;
            const char* depthResource;
            const char* viewmodelResource;
            float farDistance;
        };

        static const DepthPreviewScene scenes[] = {
            {"der_eisendrache", ":/preview/bo3_depth_der_eisendrache.jpg", ":/preview/bo3_depth_der_eisendrache_depth.png", ":/preview/bo3_depth_der_eisendrache_viewmodel.png", 5200.0f},
            {"gorod_krovi", ":/preview/bo3_depth_gorod_krovi.jpg", ":/preview/bo3_depth_gorod_krovi_depth.png", ":/preview/bo3_depth_gorod_krovi_viewmodel.png", 4800.0f},
            {"the_giant", ":/preview/bo3_depth_the_giant.jpg", ":/preview/bo3_depth_the_giant_depth.png", ":/preview/bo3_depth_the_giant_viewmodel.png", 2400.0f},
            {"zetsubou", ":/preview/bo3_depth_zetsubou.jpg", ":/preview/bo3_depth_zetsubou_depth.png", ":/preview/bo3_depth_zetsubou_viewmodel.png", 1700.0f}
        };

        const QString sceneId = requestedSceneId.trimmed().toLower();
        if(sceneId.isEmpty() || sceneId == QStringLiteral("shadows_of_evil"))
        {
            QImage captureSheet(QStringLiteral(":/preview/bo3_depth_shadows_of_evil_capture.png"));
            if(!DecodeBO3DepthCaptureSheet(captureSheet, error, true))
                return false;
            return true;
        }

        const DepthPreviewScene* scene = nullptr;
        for(const DepthPreviewScene& candidate : scenes)
        {
            if(sceneId == QString::fromLatin1(candidate.id))
            {
                scene = &candidate;
                break;
            }
        }
        if(!scene)
        {
            QImage captureSheet(QStringLiteral(":/preview/bo3_depth_shadows_of_evil_capture.png"));
            return DecodeBO3DepthCaptureSheet(captureSheet, error, true);
        }

        // Remaining legacy scenes use paired image-aligned authored/inferred
        // assets until each map receives its own real BO3 Float-Z capture.
        // Shadows of Evil is handled above by the bundled ground-truth capture.
        QImage sourceImage(QString::fromLatin1(scene->colorResource));
        QImage depthImage(QString::fromLatin1(scene->depthResource));
        QImage viewmodelImage(QString::fromLatin1(scene->viewmodelResource));
        if(sourceImage.isNull() || depthImage.isNull() || viewmodelImage.isNull())
        {
            error = L"Could not load the selected BO3 Game Depth Preview color/depth/viewmodel resources.";
            return false;
        }

        sourceImage = sourceImage.convertToFormat(QImage::Format_RGBA8888);
        depthImage = depthImage.convertToFormat(QImage::Format_Grayscale16);
        viewmodelImage = viewmodelImage.convertToFormat(QImage::Format_Grayscale8);

        constexpr UINT w = 1280;
        constexpr UINT h = 720;
        if(sourceImage.width() != static_cast<int>(w) || sourceImage.height() != static_cast<int>(h))
            sourceImage = sourceImage.scaled(static_cast<int>(w), static_cast<int>(h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if(depthImage.width() != static_cast<int>(w) || depthImage.height() != static_cast<int>(h))
            depthImage = depthImage.scaled(static_cast<int>(w), static_cast<int>(h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if(viewmodelImage.width() != static_cast<int>(w) || viewmodelImage.height() != static_cast<int>(h))
            viewmodelImage = viewmodelImage.scaled(static_cast<int>(w), static_cast<int>(h), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        constexpr float zNear = 0.1f;
        constexpr float depthHackSplit = 63.0f / 64.0f;
        const float logNear = std::log2(2.0f);
        const float logFar = std::log2(std::max(scene->farDistance, 2.001f));

        std::vector<uint8_t> color(static_cast<size_t>(w) * h * 4u, 255u);
        std::memcpy(color.data(), sourceImage.constBits(), color.size());
        std::vector<float> rawDepth(static_cast<size_t>(w) * h * 4u, 0.0f);

        auto encodeWorldRawDepth = [&](float distance)
        {
            const float safeDistance = std::max(distance, zNear + 0.001f);
            const float processed = std::clamp(zNear / safeDistance, 0.0000001f, 0.999f);
            return std::min(processed * depthHackSplit, depthHackSplit - 0.000001f);
        };

        for(UINT y = 0; y < h; ++y)
        {
            const auto* depthRow = reinterpret_cast<const quint16*>(depthImage.constScanLine(static_cast<int>(y)));
            const uchar* viewmodelRow = viewmodelImage.constScanLine(static_cast<int>(y));
            for(UINT x = 0; x < w; ++x)
            {
                const float normalizedDistance = static_cast<float>(depthRow[x]) / 65535.0f;
                const float distance = std::exp2(logNear + (logFar - logNear) * normalizedDistance);
                float raw = encodeWorldRawDepth(distance);

                // Values above 63/64 are BO3's special depth-hack/viewmodel
                // category. Use a stable interior value instead of 1.0 so debug
                // views and point samples stay unambiguous at mask boundaries.
                if(viewmodelRow[x] >= 128)
                    raw = depthHackSplit + 0.008f;

                const size_t i = (static_cast<size_t>(y) * w + x) * 4u;
                rawDepth[i + 0] = raw;
                rawDepth[i + 1] = raw;
                rawDepth[i + 2] = raw;
                rawDepth[i + 3] = 1.0f;
            }
        }

        ComPtr<ID3D11ShaderResourceView> colorSrv;
        ComPtr<ID3D11ShaderResourceView> depthSrv;
        if(!CreateTextureSRV(color.data(), w, h, colorSrv, error)) return false;
        if(!CreateFloatTextureSRV(rawDepth.data(), w, h, depthSrv, error)) return false;

        sourceSRV_ = colorSrv;
        depthSRV_ = depthSrv;
        sourceWidth_ = depthWidth_ = w;
        sourceHeight_ = depthHeight_ = h;
        sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
        sourceHdrPeakLuminance_ = 1.0f;
        sourceHdrMeanLuminance_ = 0.20f;
        depthUserLoaded_ = false;
        builtInDepthScene_ = true;
        capturedBO3DepthScene_ = false;
        previewZNear_ = zNear;
        ResetTemporalExposureState();
        return true;
    }

    bool HasUserDepthTexture() const { return depthUserLoaded_; }
    bool HasPreviewDepthTexture() const { return depthUserLoaded_ || builtInDepthScene_ || capturedBO3DepthScene_; }
    bool BuiltInDepthSceneActive() const { return builtInDepthScene_; }
    bool CapturedBO3DepthSceneActive() const { return capturedBO3DepthScene_; }
    float PreviewZNear() const { return previewZNear_; }

    ID3D11ShaderResourceView* ActivePreviewDepthSRV() const
    {
        return (depthUserLoaded_ || builtInDepthScene_ || capturedBO3DepthScene_) && depthSRV_
            ? depthSRV_.Get() : neutralDepthSRV_.Get();
    }

    void SetPaused(bool paused) { paused_ = paused; }

    void SetShaderDrivenMovementDisabled(bool disabled)
    {
        if(disabled && !shaderDrivenMovementDisabled_)
        {
            shaderMovementFrozenTime_ = elapsed_;
            shaderMovementFrozenFrame_ = frameNumber_;
            shaderMovementFrozenYaw_ = cameraYawDegrees_;
            shaderMovementFrozenPitch_ = cameraPitchDegrees_;
            shaderMovementFrozenFov_ = cameraFovDegrees_;
            shaderMovementFrozenPosition_ = cameraPosition_;
        }
        shaderDrivenMovementDisabled_ = disabled;
    }

    bool ShaderDrivenMovementDisabled() const { return shaderDrivenMovementDisabled_; }
    void SetBO3NeutralDefaults(bool enabled) { bo3NeutralDefaults_ = enabled; }

    std::vector<EditableShaderParameter> EditableShaderParameters() const
    {
        std::vector<EditableShaderParameter> result;
        auto isBuiltIn = [](const std::string& name) -> bool
        {
            const char* builtIns[] = {
                "renderTargetSize", "targetSize", "screenSize", "viewSize", "resolution",
                "invRenderTargetSize", "invTargetSize", "gameTime", "downSamples",
                "projectionMatrix", "viewMatrix", "viewProjectionMatrix", "inverseProjectionMatrix",
                "inverseViewMatrix", "inverseViewProjectionMatrix", "cameraLook", "cameraSide", "cameraUp",
                "eyeOffset", "cameraPosition", "previewCameraPos", "previewLightDir", "previewLightDirection",
                "lightDirection", "lightDir", "sunDirection", "previewLightSettings", "lightIntensity",
                "previewLightIntensity", "ambientIntensity", "previewAmbient", "shadowStrength",
                "previewShadowStrength", "environmentColor", "previewEnvironmentColor", "skyColor",
                "ambientColor", "sunColor", "lightColor", "previewSunColor", "fulbright",
                "previewFulbright", "materialColor", "relHDRExposure", "upscaledTargetSize",
                "viewportDimensions", "zNear", "previewMouse", "mouse", "previewParams"
            };
            for (const char* n : builtIns)
                if (_stricmp(name.c_str(), n) == 0) return true;
            if (name.size() == 13 && _strnicmp(name.c_str(), "scriptVector", 12) == 0 &&
                name[12] >= '0' && name[12] <= '7') return true;
            return false;
        };

        std::unordered_map<std::string, bool> seen;
        for (const auto& cb : cbuffers_)
        {
            for (const auto& v : cb.variables)
            {
                if (v.size != 4 || v.rows != 1 || v.columns != 1 || v.elements > 1) continue;
                if (!(v.varType == D3D_SVT_FLOAT || v.varType == D3D_SVT_BOOL || v.varType == D3D_SVT_INT || v.varType == D3D_SVT_UINT)) continue;
                if (isBuiltIn(v.name) || seen[v.name]) continue;
                seen[v.name] = true;
                float value = 0.0f;
                auto it = customShaderParameters_.find(v.name);
                if (it != customShaderParameters_.end()) value = it->second;
                result.push_back({v.name, v.varType == D3D_SVT_BOOL, value});
            }
        }
        std::sort(result.begin(), result.end(), [](const EditableShaderParameter& a, const EditableShaderParameter& b){ return a.name < b.name; });
        return result;
    }

    void SetShaderParameter(const std::string& name, float value)
    {
        customShaderParameters_[name] = value;
    }

    void ResetShaderParameters()
    {
        customShaderParameters_.clear();
        ApplyPreviewParameterDefaults();
    }

    const std::array<bool, 8>& DetectedScriptVectors() const { return detectedScriptVectors_; }

    std::array<float, 4> GetEffectiveScriptVector(int index) const
    {
        if (index < 0 || index >= 8) return {0, 0, 0, 0};
        const size_t i = static_cast<size_t>(index);
        if (scriptVectorCustom_[i]) return scriptVectorValues_[i];
        if (bo3NeutralDefaults_) return neutralScriptVectorDefaults_[i];
        return {0, 0, 0, 0};
    }

    void SetScriptVectorComponent(int index, int component, float value)
    {
        if (index < 0 || index >= 8 || component < 0 || component >= 4) return;
        const size_t i = static_cast<size_t>(index);
        if (!scriptVectorCustom_[i])
            scriptVectorValues_[i] = GetEffectiveScriptVector(index);
        scriptVectorValues_[i][static_cast<size_t>(component)] = value;
        scriptVectorCustom_[i] = true;
    }

    void ResetScriptVectorOverrides()
    {
        scriptVectorCustom_.fill(false);
        for (auto& v : scriptVectorValues_) v = {0, 0, 0, 0};
    }

    void SetNeutralScriptVector(int index, const std::array<float, 4>& value)
    {
        if (index < 0 || index >= 8) return;
        neutralScriptVectorDefaults_[static_cast<size_t>(index)] = value;
    }

    void SetPreviewMode(PreviewMode mode)
    {
        // The public UI has one Material target, but BO3 opaque/cutout materials
        // actually execute through the deferred GBuffer path. Once the compiled
        // pixel shader proves that contract (SV_Target1/2 are present), keep the
        // UI target as Material while rendering it through the same deferred path
        // that the exported BO3 package uses. Forward/transparent materials stay
        // on the direct-color path.
        if (mode == PreviewMode::ForwardMaterial && deferredMaterialShader_)
            previewMode_ = PreviewMode::DeferredGBuffer;
        else
            previewMode_ = mode;
    }
    PreviewMode GetPreviewMode() const { return previewMode_; }
    bool MaterialUsesDeferredGBuffer() const { return deferredMaterialShader_; }
    void SetDisplayFitMode(DisplayFitMode mode) { displayFitMode_ = mode; }
    DisplayFitMode GetDisplayFitMode() const { return displayFitMode_; }

    void SetPostFxPreviewContext(PostFxPreviewContext context) { postFxPreviewContext_ = context; }
    PostFxPreviewContext GetPostFxPreviewContext() const { return postFxPreviewContext_; }
    void SetPackagePreviewSuppressed(bool suppressed) { packagePreviewSuppressed_ = suppressed; }
    bool IsPackagePreviewSuppressed() const { return packagePreviewSuppressed_; }

    void SetPreviewResourceMappings(const QVector<bo3::PackageResourceMapping>& mappings)
    {
        previewResourceRoles_.clear();
        for(const auto& mapping : mappings)
        {
            if(mapping.resourceKind != bo3::CompiledResourceKind::Texture) continue;
            previewResourceRoles_[mapping.resourceName.toStdString()] = mapping.role;
        }
    }

    void ClearPreviewResourceMappings()
    {
        previewResourceRoles_.clear();
    }

    QSet<QString> OptimizedPixelResourceNames() const
    {
        return optimizedPixelResourceNames_;
    }

    bool OptimizedPixelResourceReflectionValid() const
    {
        return optimizedPixelResourceReflectionValid_;
    }
    void SetPostFxRuntimeExposureEV(float ev) { postFxRuntimeExposureEV_ = std::clamp(ev, -12.0f, 12.0f); }
    float GetPostFxRuntimeExposureEV() const { return postFxRuntimeExposureEV_; }
    bool SourceIsLinearHDR() const { return sourceEncoding_ == PreviewSourceEncoding::HdrSceneLinear; }
    float SourceHdrPeakLuminance() const { return sourceHdrPeakLuminance_; }
    float SourceHdrMeanLuminance() const { return sourceHdrMeanLuminance_; }

    void SetPreviewMesh(PreviewMesh mesh)
    {
        if (mesh == PreviewMesh::Custom && !customMesh_.vb) return;
        previewMesh_ = mesh;
    }
    PreviewMesh GetPreviewMesh() const { return previewMesh_; }

    bool UploadImportedPreviewMesh(const previewmodel::Mesh& imported, PreviewMeshBuffers& gpu, std::wstring& error)
    {
        if (imported.vertices.empty() || imported.indices.empty())
        {
            error = L"The imported model contains no renderable triangles.";
            return false;
        }

        std::vector<MaterialVertex> vertices;
        vertices.reserve(imported.vertices.size());
        for (const auto& v : imported.vertices)
        {
            vertices.push_back({
                {v.position[0], v.position[1], v.position[2]},
                {v.normal[0], v.normal[1], v.normal[2]},
                {v.tangent[0], v.tangent[1], v.tangent[2], v.tangent[3]},
                {v.uv[0], v.uv[1]}
            });
        }

        PreviewMeshBuffers uploaded{};
        D3D11_BUFFER_DESC vbDesc{};
        vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(MaterialVertex));
        vbDesc.Usage = D3D11_USAGE_DEFAULT;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vbData{};
        vbData.pSysMem = vertices.data();
        if (FAILED(device_->CreateBuffer(&vbDesc, &vbData, uploaded.vb.GetAddressOf())))
        {
            error = L"Could not create the imported-model vertex buffer.";
            return false;
        }

        D3D11_BUFFER_DESC ibDesc{};
        ibDesc.ByteWidth = static_cast<UINT>(imported.indices.size() * sizeof(uint32_t));
        ibDesc.Usage = D3D11_USAGE_DEFAULT;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibData{};
        ibData.pSysMem = imported.indices.data();
        if (FAILED(device_->CreateBuffer(&ibDesc, &ibData, uploaded.ib.GetAddressOf())))
        {
            error = L"Could not create the imported-model index buffer.";
            return false;
        }
        uploaded.indexCount = static_cast<UINT>(imported.indices.size());
        gpu = std::move(uploaded);
        return true;
    }

    bool LoadCustomModel(const fs::path& path, std::wstring& error)
    {
        previewmodel::Mesh imported;
        std::string importError;
        if (!previewmodel::loadModel(WideToUtf8(path.wstring()), imported, importError))
        {
            error = Utf8ToWide(importError);
            return false;
        }
        PreviewMeshBuffers gpu{};
        if (!UploadImportedPreviewMesh(imported, gpu, error)) return false;

        customMesh_ = std::move(gpu);
        customModelPath_ = path;
        customModelFormat_ = Utf8ToWide(imported.sourceFormat);
        customModelVertexCount_ = static_cast<UINT>(imported.vertices.size());
        previewMesh_ = PreviewMesh::Custom;
        cameraDistance_ = 3.25f;
        cameraPanX_ = cameraPanY_ = 0.0f;
        return true;
    }

    bool LoadApeReferenceMesh(PreviewMesh mesh, const fs::path& path, std::wstring& error)
    {
        PreviewMeshBuffers* target = nullptr;
        switch (mesh)
        {
        case PreviewMesh::Sphere: target = &apeSphereMesh_; break;
        case PreviewMesh::Cube: target = &apeCubeMesh_; break;
        case PreviewMesh::Plane: target = &apePlaneMesh_; break;
        default:
            error = L"APE reference-mesh loading currently supports Sphere, Cube, and Plane.";
            return false;
        }

        previewmodel::Mesh imported;
        std::string importError;
        if (!previewmodel::loadModel(WideToUtf8(path.wstring()), imported, importError))
        {
            error = Utf8ToWide(importError);
            return false;
        }
        if (imported.sourceFormat != "BO3 XMODEL_BIN")
        {
            error = L"APE reference meshes must be BO3 XMODEL_BIN assets.";
            return false;
        }

        PreviewMeshBuffers gpu{};
        if (!UploadImportedPreviewMesh(imported, gpu, error)) return false;
        *target = std::move(gpu);
        return true;
    }

    void ClearApeReferenceMeshes()
    {
        apeSphereMesh_ = {};
        apeCubeMesh_ = {};
        apePlaneMesh_ = {};
    }

    bool HasApeReferenceMesh(PreviewMesh mesh) const
    {
        const PreviewMeshBuffers* candidate = nullptr;
        switch (mesh)
        {
        case PreviewMesh::Sphere: candidate = &apeSphereMesh_; break;
        case PreviewMesh::Cube: candidate = &apeCubeMesh_; break;
        case PreviewMesh::Plane: candidate = &apePlaneMesh_; break;
        default: return false;
        }
        return candidate->vb && candidate->ib && candidate->indexCount > 0;
    }

    void ClearCustomModel()
    {
        customMesh_ = {};
        customModelPath_.clear(); customModelFormat_.clear(); customModelVertexCount_ = 0;
        if (previewMesh_ == PreviewMesh::Custom) previewMesh_ = PreviewMesh::Sphere;
    }
    bool HasCustomModel() const { return customMesh_.vb && customMesh_.ib && customMesh_.indexCount > 0; }
    std::wstring CustomModelPath() const { return customModelPath_.wstring(); }
    std::wstring CustomModelFormat() const { return customModelFormat_; }
    UINT CustomModelVertexCount() const { return customModelVertexCount_; }
    UINT CustomModelTriangleCount() const { return customMesh_.indexCount / 3; }

    void SetLookdevExposureEV(float ev) { lookdevExposureEV_ = std::clamp(ev, -6.0f, 6.0f); }
    float LookdevExposureEV() const { return lookdevExposureEV_; }
    void SetToneMapMode(int mode) { toneMapMode_ = std::clamp(mode, 0, 2); }
    int ToneMapMode() const { return toneMapMode_; }
    void SetGroundEnabled(bool enabled) { groundEnabled_ = enabled; }
    bool GroundEnabled() const { return groundEnabled_; }
    void SetContactShadowStrength(float value) { contactShadowStrength_ = std::clamp(value, 0.0f, 1.0f); }
    float ContactShadowStrength() const { return contactShadowStrength_; }
    void SetWireframe(bool enabled) { wireframe_ = enabled; }
    bool Wireframe() const { return wireframe_; }

    void SetGBufferView(GBufferView view) { gbufferView_ = view; }
    GBufferView GetGBufferView() const { return gbufferView_; }

    bool IsSkyShaderMode() const { return skyShaderMode_; }
    bool IsVertexOnlyShader() const { return vertexOnlyShader_; }

    void RotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees)
    {
        cameraYawDegrees_ += yawDeltaDegrees;
        while (cameraYawDegrees_ > 180.0f) cameraYawDegrees_ -= 360.0f;
        while (cameraYawDegrees_ < -180.0f) cameraYawDegrees_ += 360.0f;
        cameraPitchDegrees_ = std::clamp(cameraPitchDegrees_ + pitchDeltaDegrees, -89.0f, 89.0f);
    }

    void AdjustCameraFov(float deltaDegrees)
    {
        if (previewMode_ == PreviewMode::ForwardMaterial || previewMode_ == PreviewMode::DeferredGBuffer)
        {
            // Geometry preview uses dolly zoom instead of changing lens FOV.
            // This prevents the sphere from turning into a wide-angle/fisheye
            // shape when the user scrolls close to it.
            cameraDistance_ = std::clamp(cameraDistance_ + deltaDegrees * 0.08f, 1.8f, 12.0f);
        }
        else
        {
            cameraFovDegrees_ = std::clamp(cameraFovDegrees_ + deltaDegrees, 25.0f, 120.0f);
        }
    }

    void ResetCamera()
    {
        cameraYawDegrees_ = 0.0f;
        cameraPitchDegrees_ = 0.0f;
        cameraPanX_ = 0.0f;
        cameraPanY_ = 0.0f;
        cameraDistance_ = 4.2f;
        cameraFovDegrees_ = defaultCameraFovDegrees_;
    }

    void SetDefaultCameraFov(float fovDegrees)
    {
        defaultCameraFovDegrees_ = std::clamp(fovDegrees, 25.0f, 120.0f);
        cameraFovDegrees_ = defaultCameraFovDegrees_;
    }

    float CameraYawDegrees() const { return cameraYawDegrees_; }
    float CameraPitchDegrees() const { return cameraPitchDegrees_; }
    float CameraFovDegrees() const { return cameraFovDegrees_; }
    float CameraPanX() const { return cameraPanX_; }
    float CameraPanY() const { return cameraPanY_; }
    float CameraDistance() const { return cameraDistance_; }

    void PanCamera(float dx, float dy)
    {
        cameraPanX_ = std::clamp(cameraPanX_ + dx, -5.0f, 5.0f);
        cameraPanY_ = std::clamp(cameraPanY_ + dy, -5.0f, 5.0f);
    }

    void RotateLight(float yawDeltaDegrees, float pitchDeltaDegrees)
    {
        lightYawDegrees_ += yawDeltaDegrees;
        while (lightYawDegrees_ > 360.0f) lightYawDegrees_ -= 360.0f;
        while (lightYawDegrees_ < 0.0f) lightYawDegrees_ += 360.0f;
        lightPitchDegrees_ = std::clamp(lightPitchDegrees_ + pitchDeltaDegrees, -89.0f, 89.0f);
    }

    void SetBackgroundColor(float r, float g, float b)
    {
        backgroundColor_[0] = std::clamp(r, 0.0f, 1.0f);
        backgroundColor_[1] = std::clamp(g, 0.0f, 1.0f);
        backgroundColor_[2] = std::clamp(b, 0.0f, 1.0f);
    }
    std::array<float,3> BackgroundColor() const { return backgroundColor_; }
    void ResetBackgroundColor() { backgroundColor_ = {0.0f, 0.0f, 0.0f}; }

    void SetLightAngles(float yawDeg, float pitchDeg)
    {
        lightYawDegrees_ = yawDeg;
        lightPitchDegrees_ = std::clamp(pitchDeg, -89.0f, 89.0f);
    }
    float LightYawDegrees() const { return lightYawDegrees_; }
    float LightPitchDegrees() const { return lightPitchDegrees_; }

    void SetLightIntensity(float v) { lightIntensity_ = std::clamp(v, 0.0f, 4.0f); }
    float LightIntensity() const { return lightIntensity_; }
    void SetAmbientIntensity(float v) { ambientIntensity_ = std::clamp(v, 0.0f, 2.0f); }
    float AmbientIntensity() const { return ambientIntensity_; }
    void SetShadowStrength(float v) { shadowStrength_ = std::clamp(v, 0.0f, 1.0f); }
    float ShadowStrength() const { return shadowStrength_; }

    float PreviewFps() const { return previewFps_; }
    float GpuPassMs() const { return gpuPassMs_; }
    bool HasGpuTiming() const { return gpuPassMs_ > 0.0f; }
    bool GpuTimingSupported() const { return gpuTimersAvailable_; }
    UINT LastViewportWidth() const { return lastViewportWidth_; }
    UINT LastViewportHeight() const { return lastViewportHeight_; }
    const ShaderPerformanceStats& PerformanceStats() const { return shaderPerformanceStats_; }
    const std::wstring& AdapterName() const { return adapterName_; }

    float EstimatedGpuPassMs(UINT targetWidth, UINT targetHeight) const
    {
        if (gpuPassMs_ <= 0.0f || lastViewportWidth_ == 0 || lastViewportHeight_ == 0 ||
            targetWidth == 0 || targetHeight == 0)
            return 0.0f;
        const double sourcePixels = static_cast<double>(lastViewportWidth_) * static_cast<double>(lastViewportHeight_);
        const double targetPixels = static_cast<double>(targetWidth) * static_cast<double>(targetHeight);
        return static_cast<float>(gpuPassMs_ * (targetPixels / std::max(1.0, sourcePixels)));
    }

    void Render()
    {
        if (!context_ || !renderTarget_) return;
        const auto now = std::chrono::steady_clock::now();
        if (lastFrame_.time_since_epoch().count() == 0) lastFrame_ = now;
        float dt = std::chrono::duration<float>(now - lastFrame_).count();
        lastFrame_ = now;
        dt = std::clamp(dt, 0.0f, 0.1f);
        lastDelta_ = paused_ ? 0.0f : dt;
        if (!paused_) elapsed_ += dt;

        if(liveCaptureSource_)
        {
            QString notification;
            const bool received = liveWindowCapture_.update(context_.Get(), &notification);
            if(received)
            {
                sourceSRV_ = liveWindowCapture_.shaderResourceView();
                const bo3::LiveCaptureDiagnostics info = liveWindowCapture_.diagnostics();
                sourceWidth_ = info.width;
                sourceHeight_ = info.height;
                sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
                sourceHdrPeakLuminance_ = 1.0f;
                sourceHdrMeanLuminance_ = 0.18f;
            }
            if(!notification.isEmpty()) liveCaptureNotification_ = notification;
            const bo3::LiveCaptureDiagnostics info = liveWindowCapture_.diagnostics();
            if((info.state == bo3::LiveCaptureState::SourceLost ||
                info.state == bo3::LiveCaptureState::Error) && !info.frameAvailable)
            {
                sourceSRV_.Reset();
                sourceWidth_ = sourceHeight_ = 0;
            }
        }

        PollGpuTimers();

        const float clear[4] = {backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], 1.0f};
        context_->ClearRenderTargetView(renderTarget_.Get(), clear);

        if (previewMode_ == PreviewMode::ForwardMaterial)
        {
            RenderForwardMaterialPreview();
        }
        else if (previewMode_ == PreviewMode::DeferredGBuffer)
        {
            RenderDeferredMaterialPreview();
        }
        else if (pixelShader_)
        {
            RenderFullscreenPreview();
        }

        // Present must not block the Qt UI thread on v-sync. The old Present(1)
        // call continuously starved Qt's native caret blink/repaint timer while
        // the preview was animating. The 16 ms preview timer already paces frames.
        const HRESULT presentResult = swapChain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if(FAILED(presentResult) && presentResult != DXGI_ERROR_WAS_STILL_DRAWING)
            liveCaptureNotification_ = QString("Preview present failed (0x%1).")
                .arg(static_cast<quint32>(presentResult), 8, 16, QChar('0'));
        ++frameNumber_;
        ++fpsWindowFrames_;
        const auto fpsNow = std::chrono::steady_clock::now();
        if (fpsWindowStart_.time_since_epoch().count() == 0)
            fpsWindowStart_ = fpsNow;
        const float fpsWindowSeconds = std::chrono::duration<float>(fpsNow - fpsWindowStart_).count();
        if (fpsWindowSeconds >= 0.5f)
        {
            previewFps_ = static_cast<float>(fpsWindowFrames_) / std::max(0.001f, fpsWindowSeconds);
            fpsWindowFrames_ = 0;
            fpsWindowStart_ = fpsNow;
        }
    }

private:
    bool EnsurePostFxTarget(UINT w, UINT h)
    {
        w = std::max<UINT>(1u, w);
        h = std::max<UINT>(1u, h);
        if (postFxTexture_ && postFxRTV_ && postFxSRV_ && postFxTargetWidth_ == w && postFxTargetHeight_ == h)
            return true;

        // Make sure the old SRV is not still bound when recreating the target.
        ID3D11ShaderResourceView* nullSrv = nullptr;
        context_->PSSetShaderResources(0, 1, &nullSrv);
        postFxSRV_.Reset();
        postFxRTV_.Reset();
        postFxTexture_.Reset();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, postFxTexture_.GetAddressOf()))) return false;
        if (FAILED(device_->CreateRenderTargetView(postFxTexture_.Get(), nullptr, postFxRTV_.GetAddressOf()))) return false;
        if (FAILED(device_->CreateShaderResourceView(postFxTexture_.Get(), nullptr, postFxSRV_.GetAddressOf()))) return false;
        postFxTargetWidth_ = w;
        postFxTargetHeight_ = h;
        return true;
    }

    bool EnsureRuntimeSceneTarget(UINT w, UINT h)
    {
        w = std::max<UINT>(1u, w);
        h = std::max<UINT>(1u, h);
        if (runtimeSceneTexture_ && runtimeSceneRTV_ && runtimeSceneSRV_ &&
            runtimeSceneWidth_ == w && runtimeSceneHeight_ == h)
            return true;

        UnbindAllPixelResources();
        runtimeSceneSRV_.Reset();
        runtimeSceneRTV_.Reset();
        runtimeSceneTexture_.Reset();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(&desc, nullptr, runtimeSceneTexture_.GetAddressOf()))) return false;
        if (FAILED(device_->CreateRenderTargetView(runtimeSceneTexture_.Get(), nullptr, runtimeSceneRTV_.GetAddressOf()))) return false;
        if (FAILED(device_->CreateShaderResourceView(runtimeSceneTexture_.Get(), nullptr, runtimeSceneSRV_.GetAddressOf()))) return false;
        runtimeSceneWidth_ = w;
        runtimeSceneHeight_ = h;
        return true;
    }

    bool RenderRuntimeResolvedSceneInput()
    {
        if (postFxPreviewContext_ != PostFxPreviewContext::Bo3RuntimeResolvedScene || !sourceSRV_) return true;
        const UINT w = std::max<UINT>(1u, sourceWidth_);
        const UINT h = std::max<UINT>(1u, sourceHeight_);
        if (!EnsureRuntimeSceneTarget(w, h)) return false;

        UnbindAllPixelResources();
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(w);
        vp.Height = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        const float clear[4] = {0,0,0,1};
        context_->ClearRenderTargetView(runtimeSceneRTV_.Get(), clear);
        context_->OMSetRenderTargets(1, runtimeSceneRTV_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &vp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(blitVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(runtimeSceneEncodePixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());

        const float exposureScale = std::exp2(postFxRuntimeExposureEV_);
        const std::array<float,4> runtimeSceneParams{
            sourceEncoding_ == PreviewSourceEncoding::HdrSceneLinear ? 1.0f : 0.0f,
            exposureScale, 0.0f, 0.0f};
        if (runtimeSceneParamsBuffer_)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context_->Map(runtimeSceneParamsBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, runtimeSceneParams.data(), sizeof(runtimeSceneParams));
                context_->Unmap(runtimeSceneParamsBuffer_.Get(), 0);
            }
            ID3D11Buffer* runtimeParams = runtimeSceneParamsBuffer_.Get();
            context_->PSSetConstantBuffers(0, 1, &runtimeParams);
        }

        ID3D11ShaderResourceView* source = sourceSRV_.Get();
        context_->PSSetShaderResources(0, 1, &source);
        context_->Draw(3, 0);
        UnbindAllPixelResources();
        // PreviewRuntimeSceneParams belongs only to this internal fullscreen encode pass.
        // D3D11/SM5 exposes constant-buffer slots b0-b13, so use b0 here and
        // immediately clear it before the user's reflected cbuffers are rebound.
        ID3D11Buffer* nullRuntimeParams = nullptr;
        context_->PSSetConstantBuffers(0, 1, &nullRuntimeParams);
        return true;
    }

    bool EnsureTemporalExposureTargets()
    {
        if (!device_ || !context_) return false;
        if (temporalExposureTextures_[0] && temporalExposureTextures_[1] &&
            temporalExposureRTVs_[0] && temporalExposureRTVs_[1] &&
            temporalExposureSRVs_[0] && temporalExposureSRVs_[1])
            return true;

        for (size_t i = 0; i < 2; ++i)
        {
            temporalExposureTextures_[i].Reset();
            temporalExposureRTVs_[i].Reset();
            temporalExposureSRVs_[i].Reset();

            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1;
            td.Height = 1;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            if (FAILED(device_->CreateTexture2D(&td, nullptr, temporalExposureTextures_[i].GetAddressOf())))
                return false;
            if (FAILED(device_->CreateRenderTargetView(temporalExposureTextures_[i].Get(), nullptr, temporalExposureRTVs_[i].GetAddressOf())))
                return false;
            if (FAILED(device_->CreateShaderResourceView(temporalExposureTextures_[i].Get(), nullptr, temporalExposureSRVs_[i].GetAddressOf())))
                return false;
        }

        ResetTemporalExposureState();
        return true;
    }

    void ResetTemporalExposureState()
    {
        temporalExposureReadIndex_ = 0;
        if (!context_) return;
        const float clear[4] = {0, 0, 0, 0};
        for (auto& rtv : temporalExposureRTVs_)
            if (rtv) context_->ClearRenderTargetView(rtv.Get(), clear);
    }

    void RenderTemporalExposureStatePass()
    {
        if (!temporalExposureMode_ || !temporalExposureStatePS_ || !sourceSRV_) return;
        if (!EnsureTemporalExposureTargets()) return;

        const int previous = temporalExposureReadIndex_;
        const int current = 1 - previous;

        // Ensure neither feedback texture is still bound as an SRV before using
        // the current one as a render target.
        UnbindAllPixelResources();

        D3D11_VIEWPORT stateVp{};
        stateVp.Width = 1.0f;
        stateVp.Height = 1.0f;
        stateVp.MinDepth = 0.0f;
        stateVp.MaxDepth = 1.0f;

        context_->OMSetRenderTargets(1, temporalExposureRTVs_[current].GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &stateVp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(postFxVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(temporalExposureStatePS_.Get(), nullptr, 0);

        std::array<ID3D11SamplerState*, 2> samplers{sampler_.Get(), sampler_.Get()};
        context_->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        ID3D11ShaderResourceView* postFxSource =
            (postFxPreviewContext_ == PostFxPreviewContext::Bo3RuntimeResolvedScene && runtimeSceneSRV_)
                ? runtimeSceneSRV_.Get() : sourceSRV_.Get();
        std::array<ID3D11ShaderResourceView*, 2> srvs{postFxSource, temporalExposureSRVs_[previous].Get()};
        context_->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());

        // Use source dimensions for BO3-style renderTargetSize globals even
        // though the feedback target itself is only 1x1.
        D3D11_VIEWPORT sourceVp{};
        sourceVp.Width = static_cast<float>(std::max<UINT>(1u, sourceWidth_));
        sourceVp.Height = static_cast<float>(std::max<UINT>(1u, sourceHeight_));
        sourceVp.MinDepth = 0.0f;
        sourceVp.MaxDepth = 1.0f;
        UpdateConstantBuffers(sourceVp);

        context_->Draw(3, 0);
        UnbindAllPixelResources();
        temporalExposureReadIndex_ = current;
    }

    D3D11_VIEWPORT MakeDisplayViewport(UINT contentW, UINT contentH) const
    {
        return MakeDisplayViewportInRect(0.0f, 0.0f,
            static_cast<float>(std::max<LONG>(1, width_)),
            static_cast<float>(std::max<LONG>(1, height_)), contentW, contentH);
    }

    D3D11_VIEWPORT MakeDisplayViewportInRect(float rectX, float rectY, float rectW, float rectH,
                                             UINT contentW, UINT contentH) const
    {
        float vpX = rectX, vpY = rectY;
        float vpW = std::max(1.0f, rectW);
        float vpH = std::max(1.0f, rectH);
        if (contentW > 0 && contentH > 0)
        {
            const float sourceAspect = static_cast<float>(contentW) / static_cast<float>(contentH);
            const float paneAspect = vpW / std::max(1.0f, vpH);
            if (displayFitMode_ == DisplayFitMode::Fill)
            {
                // Fill the preview like a modern editor viewport: preserve aspect
                // ratio and crop only the excess edge instead of surrounding the
                // image with large black letterbox bars. Users can switch back to
                // Fit View when they need to inspect every source pixel.
                if (paneAspect > sourceAspect)
                {
                    vpH = vpW / sourceAspect;
                    vpY = rectY + (rectH - vpH) * 0.5f;
                }
                else
                {
                    vpW = vpH * sourceAspect;
                    vpX = rectX + (rectW - vpW) * 0.5f;
                }
            }
            else
            {
                if (paneAspect > sourceAspect)
                {
                    vpW = vpH * sourceAspect;
                    vpX = rectX + (rectW - vpW) * 0.5f;
                }
                else
                {
                    vpH = vpW / sourceAspect;
                    vpY = rectY + (rectH - vpH) * 0.5f;
                }
            }
        }
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = vpX;
        vp.TopLeftY = vpY;
        vp.Width = std::max(1.0f, vpW);
        vp.Height = std::max(1.0f, vpH);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        return vp;
    }

    bool EnsureComparisonScissorRasterizer()
    {
        if(comparisonScissorRasterizer_) return true;
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.DepthClipEnable = TRUE;
        desc.ScissorEnable = TRUE;
        return SUCCEEDED(device_->CreateRasterizerState(
            &desc, comparisonScissorRasterizer_.GetAddressOf()));
    }

    void DrawDisplayTexture(ID3D11ShaderResourceView* srv, ID3D11PixelShader* shader,
                            const D3D11_VIEWPORT& viewport, const RECT* scissor = nullptr)
    {
        if(!srv || !shader) return;
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &viewport);
        if(scissor && EnsureComparisonScissorRasterizer())
        {
            context_->RSSetState(comparisonScissorRasterizer_.Get());
            context_->RSSetScissorRects(1, scissor);
        }
        else
        {
            context_->RSSetState(nullptr);
        }
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(blitVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(shader, nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, &srv);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* nullSrv = nullptr;
        context_->PSSetShaderResources(0, 1, &nullSrv);
        context_->RSSetState(nullptr);
    }

    void BindPostFxResources()
    {
        ID3D11ShaderResourceView* postFxSource = sourceSRV_.Get();
        if (previewMode_ == PreviewMode::PostFX &&
            postFxPreviewContext_ == PostFxPreviewContext::Bo3RuntimeResolvedScene && runtimeSceneSRV_)
            postFxSource = runtimeSceneSRV_.Get();

        std::array<ID3D11SamplerState*, 16> samplers{};
        samplers.fill(sampler_.Get());
        for (int channel = 0; channel < kShadertoyChannelCount; ++channel)
        {
            const UINT samplerSlot = static_cast<UINT>(channel + 2);
            if (samplerSlot < samplers.size())
                samplers[samplerSlot] = shadertoyChannelRepeat_[static_cast<size_t>(channel)] && materialWrapSampler_.Get() != nullptr
                    ? materialWrapSampler_.Get() : sampler_.Get();
        }
        context_->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());

        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> srvs{};
        for (const auto& resource : resources_)
        {
            ID3D11ShaderResourceView* fallback = NeutralForDimension(resource.dimension);
            for (UINT n = 0; n < std::max(1u, resource.bindCount); ++n)
            {
                const UINT slot = resource.slot + n;
                if (slot >= srvs.size()) break;
                srvs[slot] = fallback;
            }

            const auto mappedRole = previewResourceRoles_.find(resource.name);
            if(mappedRole != previewResourceRoles_.end() && resource.slot < srvs.size())
            {
                // Live Preview As must render the same resource contract that
                // the temporary techset validates. A channel mapped to
                // resolvedScene therefore receives t0 even if a Shadertoy test
                // image is still loaded in that channel slot.
                if(mappedRole->second == bo3::PackageResourceRole::ResolvedScene &&
                   Is2DLike(resource.dimension))
                {
                    srvs[resource.slot] = postFxSource;
                    continue;
                }
                if(mappedRole->second == bo3::PackageResourceRole::FloatDepth &&
                   Is2DLike(resource.dimension))
                {
                    srvs[resource.slot] = ActivePreviewDepthSRV();
                    continue;
                }
                if(mappedRole->second == bo3::PackageResourceRole::Ignore)
                    continue;
                // MaterialImage falls through to the normal iChannel/material
                // preview binding below.
            }

            if (resource.name == "frameBuffer" && Is2DLike(resource.dimension) && resource.slot < srvs.size())
                srvs[resource.slot] = postFxSource;
            else if (temporalExposureMode_ && resource.name == "exposureHistory" &&
                     Is2DLike(resource.dimension) && resource.slot < srvs.size() &&
                     temporalExposureSRVs_[temporalExposureReadIndex_])
                srvs[resource.slot] = temporalExposureSRVs_[temporalExposureReadIndex_].Get();
            else if (resource.slot == 1 && Is2DLike(resource.dimension))
                srvs[1] = ActivePreviewDepthSRV();
            else if (resource.name.rfind("iChannel", 0) == 0 && resource.name.size() == 9)
            {
                const char digit = resource.name[8];
                if (digit >= '0' && digit <= '3')
                {
                    const int channel = digit - '0';
                    ID3D11ShaderResourceView* channelSrv = shadertoyChannelSRVs_[static_cast<size_t>(channel)].Get();
                    if (channelSrv && resource.slot < srvs.size())
                        srvs[resource.slot] = channelSrv;
                }
            }
        }
        if (!srvs[0]) srvs[0] = postFxSource;
        if (!srvs[1]) srvs[1] = ActivePreviewDepthSRV();
        context_->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
    }

    void UnbindAllPixelResources()
    {
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> nullSRV{};
        context_->PSSetShaderResources(0, static_cast<UINT>(nullSRV.size()), nullSRV.data());
    }

    void RenderFullscreenPreview()
    {
        if (!pixelShader_ || packagePreviewSuppressed_) return;

        if (previewMode_ == PreviewMode::PostFX &&
            postFxPreviewContext_ == PostFxPreviewContext::Bo3RuntimeResolvedScene &&
            !RenderRuntimeResolvedSceneInput())
            return;

        if (temporalExposureMode_ && previewMode_ == PreviewMode::PostFX)
            RenderTemporalExposureStatePass();

        // Sky shaders are directional and are intentionally rendered straight to
        // the window. PostFX shaders, however, must see BO3-like native pixel
        // coordinates. Rendering them at the UI pane size breaks Texture2D.Load()
        // effects even though they compile successfully.
        const bool directionalFullscreen = previewMode_ == PreviewMode::Sky || skyShaderMode_;
        if (directionalFullscreen || sourceWidth_ == 0 || sourceHeight_ == 0)
        {
            const D3D11_VIEWPORT vp = MakeDisplayViewport(sourceWidth_, sourceHeight_);
            lastViewportWidth_ = static_cast<UINT>(std::max(1.0f, vp.Width));
            lastViewportHeight_ = static_cast<UINT>(std::max(1.0f, vp.Height));
            context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
            context_->RSSetViewports(1, &vp);
            context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(directionalFullscreen ? skyVertexShader_.Get() : postFxVertexShader_.Get(), nullptr, 0);
            UpdateCameraVertexBuffer(vp);
            context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
            BindPostFxResources();
            UpdateConstantBuffers(vp);
            BeginGpuTimer();
            context_->Draw(3, 0);
            EndGpuTimer();
            UnbindAllPixelResources();
            return;
        }

        UINT passW = sourceWidth_;
        UINT passH = sourceHeight_;
        if (downsamplePassDetected_)
        {
            const UINT factor = std::max<UINT>(1u, static_cast<UINT>(std::lround(downSamplesValue_ + 1.0f)));
            if (upsamplingPassDetected_)
            {
                passW = std::max<UINT>(1u, sourceWidth_ * factor);
                passH = std::max<UINT>(1u, sourceHeight_ * factor);
            }
            else
            {
                passW = std::max<UINT>(1u, sourceWidth_ / factor);
                passH = std::max<UINT>(1u, sourceHeight_ / factor);
            }
        }

        if (!EnsurePostFxTarget(passW, passH)) return;

        D3D11_VIEWPORT passVp{};
        passVp.TopLeftX = 0.0f;
        passVp.TopLeftY = 0.0f;
        passVp.Width = static_cast<float>(passW);
        passVp.Height = static_cast<float>(passH);
        passVp.MinDepth = 0.0f;
        passVp.MaxDepth = 1.0f;
        lastViewportWidth_ = passW;
        lastViewportHeight_ = passH;

        const float passClear[4] = {0, 0, 0, 1};
        context_->ClearRenderTargetView(postFxRTV_.Get(), passClear);
        context_->OMSetRenderTargets(1, postFxRTV_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &passVp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(postFxVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        BindPostFxResources();
        UpdateConstantBuffers(passVp);
        BeginGpuTimer();
        context_->Draw(3, 0);
        EndGpuTimer();
        UnbindAllPixelResources();

        // Scale the completed BO3-sized pass into the editor pane only after the
        // shader has run. This keeps SV_Position and Texture2D.Load coordinates
        // independent of how large the user makes the Preview dock.
        ID3D11PixelShader* processedDisplayShader =
            previewMode_ == PreviewMode::PostFX &&
            postFxPreviewContext_ == PostFxPreviewContext::Bo3RuntimeResolvedScene
                ? runtimeSceneDecodePixelShader_.Get() : blitPixelShader_.Get();
        if(liveCaptureSource_ && liveComparisonMode_ == LiveComparisonMode::SideBySide)
        {
            const float half = static_cast<float>(width_) * 0.5f;
            const D3D11_VIEWPORT originalVp = MakeDisplayViewportInRect(
                0.0f, 0.0f, half, static_cast<float>(height_), sourceWidth_, sourceHeight_);
            const D3D11_VIEWPORT processedVp = MakeDisplayViewportInRect(
                half, 0.0f, static_cast<float>(width_) - half,
                static_cast<float>(height_), passW, passH);
            DrawDisplayTexture(sourceSRV_.Get(), blitPixelShader_.Get(), originalVp);
            DrawDisplayTexture(postFxSRV_.Get(), processedDisplayShader, processedVp);
        }
        else if(liveCaptureSource_ && liveComparisonMode_ == LiveComparisonMode::Split)
        {
            const D3D11_VIEWPORT displayVp = MakeDisplayViewport(passW, passH);
            DrawDisplayTexture(sourceSRV_.Get(), blitPixelShader_.Get(), displayVp);
            RECT scissor{};
            scissor.left = static_cast<LONG>(std::lround(
                displayVp.TopLeftX + displayVp.Width * liveSplitFraction_));
            scissor.top = static_cast<LONG>(std::floor(displayVp.TopLeftY));
            scissor.right = static_cast<LONG>(std::ceil(displayVp.TopLeftX + displayVp.Width));
            scissor.bottom = static_cast<LONG>(std::ceil(displayVp.TopLeftY + displayVp.Height));
            DrawDisplayTexture(postFxSRV_.Get(), processedDisplayShader, displayVp, &scissor);
        }
        else
        {
            const D3D11_VIEWPORT displayVp = MakeDisplayViewport(passW, passH);
            DrawDisplayTexture(postFxSRV_.Get(), processedDisplayShader, displayVp);
        }
    }

    static std::wstring Hex(HRESULT hr)
    {
        wchar_t buf[16]{};
        swprintf_s(buf, L"%08X", static_cast<unsigned int>(hr));
        return buf;
    }

    struct GpuTimerSlot
    {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> start;
        ComPtr<ID3D11Query> end;
        bool pending = false;
        uint64_t generation = 0;
    };

    void QueryAdapterName()
    {
        adapterName_ = L"Direct3D 11 GPU";
        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_.As(&dxgiDevice)) || !dxgiDevice) return;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) || !adapter) return;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(adapter->GetDesc(&desc)))
            adapterName_ = desc.Description;
    }

    void InitializeGpuTimers()
    {
        gpuTimersAvailable_ = true;
        for (auto& slot : gpuTimerSlots_)
        {
            D3D11_QUERY_DESC disjoint{};
            disjoint.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            D3D11_QUERY_DESC timestamp{};
            timestamp.Query = D3D11_QUERY_TIMESTAMP;
            if (FAILED(device_->CreateQuery(&disjoint, slot.disjoint.GetAddressOf())) ||
                FAILED(device_->CreateQuery(&timestamp, slot.start.GetAddressOf())) ||
                FAILED(device_->CreateQuery(&timestamp, slot.end.GetAddressOf())))
            {
                gpuTimersAvailable_ = false;
                for (auto& clearSlot : gpuTimerSlots_)
                {
                    clearSlot.disjoint.Reset();
                    clearSlot.start.Reset();
                    clearSlot.end.Reset();
                    clearSlot.pending = false;
                }
                break;
            }
        }
    }

    void PollGpuTimers()
    {
        if (!gpuTimersAvailable_) return;
        for (auto& slot : gpuTimerSlots_)
        {
            if (!slot.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData{};
            const HRESULT disjointHr = context_->GetData(
                slot.disjoint.Get(), &disjointData, sizeof(disjointData), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (disjointHr == S_FALSE) continue;
            if (FAILED(disjointHr))
            {
                slot.pending = false;
                continue;
            }

            UINT64 start = 0, end = 0;
            const HRESULT startHr = context_->GetData(
                slot.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const HRESULT endHr = context_->GetData(
                slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (startHr == S_FALSE || endHr == S_FALSE) continue;
            slot.pending = false;
            if (slot.generation != shaderGeneration_)
                continue;
            if (FAILED(startHr) || FAILED(endHr) || disjointData.Disjoint ||
                disjointData.Frequency == 0 || end < start)
                continue;

            const double milliseconds =
                (static_cast<double>(end - start) / static_cast<double>(disjointData.Frequency)) * 1000.0;
            const float sample = static_cast<float>(milliseconds);
            if (sample >= 0.0f && sample < 1000.0f)
                gpuPassMs_ = gpuPassMs_ <= 0.0f ? sample : gpuPassMs_ * 0.82f + sample * 0.18f;
        }
    }

    void BeginGpuTimer()
    {
        gpuTimerActive_ = false;
        if (!gpuTimersAvailable_) return;
        auto& slot = gpuTimerSlots_[gpuTimerWriteIndex_];
        if (slot.pending) return;
        slot.generation = shaderGeneration_;
        context_->Begin(slot.disjoint.Get());
        context_->End(slot.start.Get());
        gpuTimerActive_ = true;
    }

    void EndGpuTimer()
    {
        if (!gpuTimerActive_ || !gpuTimersAvailable_) return;
        auto& slot = gpuTimerSlots_[gpuTimerWriteIndex_];
        context_->End(slot.end.Get());
        context_->End(slot.disjoint.Get());
        slot.pending = true;
        gpuTimerWriteIndex_ = (gpuTimerWriteIndex_ + 1) % gpuTimerSlots_.size();
        gpuTimerActive_ = false;
    }

    bool CreateRenderTarget(std::wstring& error)
    {
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr))
        {
            error = L"Swap-chain GetBuffer failed.";
            return false;
        }
        hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateRenderTargetView failed.";
            return false;
        }
        return true;
    }

    bool CreateMaterialCameraBuffer(std::wstring& error)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 176; // two float4x4 matrices + camera/light + UV transform
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, materialCameraBuffer_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateBuffer failed for the material preview camera constant buffer.";
            return false;
        }
        return true;
    }

    bool CreateDeferredLightBuffer(std::wstring& error)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 128;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, deferredLightBuffer_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateBuffer failed for the deferred-light preview constant buffer.";
            return false;
        }
        return true;
    }

    void UpdateDeferredLightBuffer()
    {
        if (!deferredLightBuffer_) return;
        struct DeferredLightData
        {
            DirectX::XMFLOAT4 lightDirIntensity;
            DirectX::XMFLOAT4 ambientShadow;
            DirectX::XMFLOAT4 environmentAmbient;
            DirectX::XMFLOAT4 lightColorFulbright;
            DirectX::XMFLOAT4 backgroundColor;
            DirectX::XMFLOAT4 lookdevSettings;
            DirectX::XMFLOAT4 debugSettings;
            DirectX::XMFLOAT4 apeSettings;
        } data{};
        const float ly = DirectX::XMConvertToRadians(lightYawDegrees_);
        const float lp = DirectX::XMConvertToRadians(lightPitchDegrees_);
        const float lx = std::cos(lp) * std::cos(ly);
        const float lyy = std::sin(lp);
        const float lz = std::cos(lp) * std::sin(ly);
        data.lightDirIntensity = {lx, lyy, lz, lightIntensity_};
        data.ambientShadow = {ambientIntensity_, shadowStrength_, environmentAffectsLighting_ ? 1.0f : 0.0f, environmentEnabled_ ? 1.0f : 0.0f};
        auto ambientColor = (environmentAffectsLighting_ && environmentEnabled_) ? environmentAverageColor_ : std::array<float,3>{0.26f, 0.26f, 0.28f};
        if (environmentIsEXR_ && materialPreviewProfile_ == MaterialPreviewProfile::LookDev)
        {
            for (float& c : ambientColor) c = c / (1.0f + std::max(0.0f, c));
        }
        const auto sunColor = useExplicitLightColor_
            ? lightColor_
            : ((environmentAffectsLighting_ && environmentEnabled_) ? environmentSunColor_ : std::array<float,3>{1.0f, 1.0f, 1.0f});
        data.environmentAmbient = {ambientColor[0], ambientColor[1], ambientColor[2], previewMode_ == PreviewMode::ForwardMaterial ? 1.0f : 0.0f};
        data.lightColorFulbright = {sunColor[0], sunColor[1], sunColor[2], fulbright_ ? 1.0f : 0.0f};
        data.backgroundColor = {backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], 1.0f};
        data.lookdevSettings = {lookdevExposureEV_, static_cast<float>(toneMapMode_), groundEnabled_ ? 1.0f : 0.0f, contactShadowStrength_};
        data.debugSettings = {static_cast<float>(gbufferView_), static_cast<float>(materialPreviewProfile_), 0.0f, 0.0f};
        data.apeSettings = {environmentRotationDegrees_ * (3.14159265358979323846f / 180.0f), 0.0f, 0.0f, 0.0f};
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(deferredLightBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &data, sizeof(data));
            context_->Unmap(deferredLightBuffer_.Get(), 0);
        }
    }

    bool CreateGBufferTargets(std::wstring& error)
    {
        for (auto& texture : gbufferTextures_) texture.Reset();
        for (auto& rtv : gbufferRTVs_) rtv.Reset();
        for (auto& srv : gbufferSRVs_) srv.Reset();
        materialDepthTexture_.Reset();
        materialDepthDSV_.Reset();
        materialDepthSRV_.Reset();

        const UINT w = std::max<UINT>(1u, static_cast<UINT>(width_));
        const UINT h = std::max<UINT>(1u, static_cast<UINT>(height_));

        D3D11_TEXTURE2D_DESC colorDesc{};
        colorDesc.Width = w;
        colorDesc.Height = h;
        colorDesc.MipLevels = 1;
        colorDesc.ArraySize = 1;
        colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Usage = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        for (size_t i = 0; i < gbufferTextures_.size(); ++i)
        {
            HRESULT hr = device_->CreateTexture2D(&colorDesc, nullptr, gbufferTextures_[i].GetAddressOf());
            if (FAILED(hr))
            {
                error = L"Could not create a GBuffer render target texture.";
                return false;
            }
            hr = device_->CreateRenderTargetView(gbufferTextures_[i].Get(), nullptr, gbufferRTVs_[i].GetAddressOf());
            if (FAILED(hr))
            {
                error = L"Could not create a GBuffer render target view.";
                return false;
            }
            hr = device_->CreateShaderResourceView(gbufferTextures_[i].Get(), nullptr, gbufferSRVs_[i].GetAddressOf());
            if (FAILED(hr))
            {
                error = L"Could not create a GBuffer shader-resource view.";
                return false;
            }
        }

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = w;
        depthDesc.Height = h;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device_->CreateTexture2D(&depthDesc, nullptr, materialDepthTexture_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not create a GBuffer depth texture.";
            return false;
        }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = device_->CreateDepthStencilView(materialDepthTexture_.Get(), &dsvDesc, materialDepthDSV_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not create a GBuffer depth-stencil view.";
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
        depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Texture2D.MostDetailedMip = 0;
        depthSrvDesc.Texture2D.MipLevels = 1;
        hr = device_->CreateShaderResourceView(materialDepthTexture_.Get(), &depthSrvDesc, materialDepthSRV_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Could not create a GBuffer depth shader-resource view.";
            return false;
        }

        return true;
    }

    bool CreatePreviewMeshes(std::wstring& error)
    {
        auto buildMesh = [&](const std::vector<MaterialVertex>& vertices, const std::vector<uint32_t>& indices, PreviewMeshBuffers& out) -> bool
        {
            out = {};
            if (vertices.empty() || indices.empty()) return false;

            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(MaterialVertex));
            vbDesc.Usage = D3D11_USAGE_DEFAULT;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData{};
            vbData.pSysMem = vertices.data();
            HRESULT hr = device_->CreateBuffer(&vbDesc, &vbData, out.vb.GetAddressOf());
            if (FAILED(hr)) return false;

            D3D11_BUFFER_DESC ibDesc{};
            ibDesc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint32_t));
            ibDesc.Usage = D3D11_USAGE_DEFAULT;
            ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ibData{};
            ibData.pSysMem = indices.data();
            hr = device_->CreateBuffer(&ibDesc, &ibData, out.ib.GetAddressOf());
            if (FAILED(hr)) return false;
            out.indexCount = static_cast<UINT>(indices.size());
            return true;
        };

        std::vector<MaterialVertex> vertices;
        std::vector<uint32_t> indices;

        // Plane - dense 64x64 grid so vertex displacement/height shaders
        // have enough actual geometry to deform rather than only four corners.
        constexpr int planeDivisions = 64;
        vertices.clear(); indices.clear();
        vertices.reserve(static_cast<size_t>(planeDivisions + 1) * (planeDivisions + 1));
        indices.reserve(static_cast<size_t>(planeDivisions) * planeDivisions * 6);
        for (int z = 0; z <= planeDivisions; ++z)
        {
            const float v = static_cast<float>(z) / static_cast<float>(planeDivisions);
            for (int x = 0; x <= planeDivisions; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(planeDivisions);
                vertices.push_back({
                    {-1.0f + u * 2.0f, 0.0f, -1.0f + v * 2.0f},
                    {0.0f, 1.0f, 0.0f},
                    {1.0f, 0.0f, 0.0f, 1.0f},
                    {u, 1.0f - v}
                });
            }
        }
        for (int z = 0; z < planeDivisions; ++z)
        {
            for (int x = 0; x < planeDivisions; ++x)
            {
                const uint32_t a = static_cast<uint32_t>(z * (planeDivisions + 1) + x);
                const uint32_t b = a + 1;
                const uint32_t c = a + static_cast<uint32_t>(planeDivisions + 1);
                const uint32_t d = c + 1;
                indices.insert(indices.end(), {a, b, d, a, d, c});
            }
        }
        if (!buildMesh(vertices, indices, planeMesh_))
        {
            error = L"Could not create the subdivided plane preview mesh.";
            return false;
        }

        // Vertical material card - useful for foliage, grass, fences, particles,
        // and other alpha-cutout surface shaders. It faces the default -Z camera.
        constexpr int cardDivisions = 32;
        vertices.clear(); indices.clear();
        vertices.reserve(static_cast<size_t>(cardDivisions + 1) * (cardDivisions + 1));
        indices.reserve(static_cast<size_t>(cardDivisions) * cardDivisions * 6);
        for (int y = 0; y <= cardDivisions; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(cardDivisions);
            for (int x = 0; x <= cardDivisions; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(cardDivisions);
                vertices.push_back({
                    {-1.0f + u * 2.0f, -1.0f + v * 2.0f, 0.0f},
                    {0.0f, 0.0f, -1.0f},
                    {1.0f, 0.0f, 0.0f, 1.0f},
                    {u, v}
                });
            }
        }
        for (int y = 0; y < cardDivisions; ++y)
        {
            for (int x = 0; x < cardDivisions; ++x)
            {
                const uint32_t a = static_cast<uint32_t>(y * (cardDivisions + 1) + x);
                const uint32_t b = a + 1;
                const uint32_t c = a + static_cast<uint32_t>(cardDivisions + 1);
                const uint32_t d = c + 1;
                indices.insert(indices.end(), {a, d, b, a, c, d});
            }
        }
        if (!buildMesh(vertices, indices, cardMesh_))
        {
            error = L"Could not create the vertical material-card preview mesh.";
            return false;
        }

        // Cube - every face is a 32x32 grid. This intentionally duplicates edge
        // vertices so each face keeps clean UVs/tangents while displacement can
        // still deform the surface densely.
        constexpr int cubeDivisions = 32;
        vertices.clear(); indices.clear();
        auto lerp3 = [](const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return DirectX::XMFLOAT3{
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        };
        auto addSubdivFace = [&](DirectX::XMFLOAT3 n, DirectX::XMFLOAT4 t,
                                 DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b,
                                 DirectX::XMFLOAT3 c, DirectX::XMFLOAT3 d)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            for (int y = 0; y <= cubeDivisions; ++y)
            {
                const float v = static_cast<float>(y) / static_cast<float>(cubeDivisions);
                const DirectX::XMFLOAT3 left = lerp3(a, d, v);
                const DirectX::XMFLOAT3 right = lerp3(b, c, v);
                for (int x = 0; x <= cubeDivisions; ++x)
                {
                    const float u = static_cast<float>(x) / static_cast<float>(cubeDivisions);
                    const DirectX::XMFLOAT3 pos = lerp3(left, right, u);
                    vertices.push_back({pos, n, t, {u, 1.0f - v}});
                }
            }
            for (int y = 0; y < cubeDivisions; ++y)
            {
                for (int x = 0; x < cubeDivisions; ++x)
                {
                    const uint32_t i0 = base + static_cast<uint32_t>(y * (cubeDivisions + 1) + x);
                    const uint32_t i1 = i0 + 1;
                    const uint32_t i2 = i0 + static_cast<uint32_t>(cubeDivisions + 1);
                    const uint32_t i3 = i2 + 1;
                    indices.insert(indices.end(), {i0, i1, i3, i0, i3, i2});
                }
            }
        };
        addSubdivFace({ 0, 0, 1}, {1,0,0,1}, {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1});
        addSubdivFace({ 0, 0,-1}, {-1,0,0,1}, { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1});
        addSubdivFace({ 1, 0, 0}, {0,0,-1,1}, { 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1});
        addSubdivFace({-1, 0, 0}, {0,0,1,1}, {-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1});
        addSubdivFace({ 0, 1, 0}, {1,0,0,1}, {-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1});
        addSubdivFace({ 0,-1, 0}, {1,0,0,1}, {-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1});
        if (!buildMesh(vertices, indices, cubeMesh_))
        {
            error = L"Could not create the subdivided cube preview mesh.";
            return false;
        }

        // Sphere
        // Proper seam-safe equirectangular UV sphere. The geometric poles are
        // duplicated per longitude slice so each cap triangle owns a pole UV
        // matching that triangle's longitude. Sharing one pole vertex (and one
        // U value) across the whole cap causes huge interpolation wedges in
        // procedural/Shadertoy materials that reconstruct direction from UVs.
        vertices.clear(); indices.clear();
        const int slices = 96;
        const int stacks = 64;
        auto makeSphereTangent = [](float theta) -> DirectX::XMFLOAT4
        {
            const float tx = -std::sin(theta);
            const float tz = std::cos(theta);
            const float len = std::sqrt(tx * tx + tz * tz);
            if (len < 0.000001f)
                return {1.0f, 0.0f, 0.0f, 1.0f};
            return {tx / len, 0.0f, tz / len, 1.0f};
        };

        // Interior latitude rings retain duplicated U=0/U=1 seam vertices.
        // Their positions/normals are identical, while UVs intentionally wrap.
        auto ringStart = [&](int ring) -> uint32_t
        {
            return static_cast<uint32_t>(ring - 1) * static_cast<uint32_t>(slices + 1);
        };
        for (int y = 1; y < stacks; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(stacks);
            const float phi = v * 3.1415926535f;
            const float py = std::cos(phi);
            const float pr = std::sin(phi);
            for (int x = 0; x <= slices; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(slices);
                const float theta = u * 6.283185307f;
                const float px = std::cos(theta) * pr;
                const float pz = std::sin(theta) * pr;
                DirectX::XMFLOAT3 normal{px, py, pz};
                vertices.push_back({{px, py, pz}, normal, makeSphereTangent(theta), {u, 1.0f - v}});
            }
        }

        // Interior quads.
        for (int y = 1; y < stacks - 1; ++y)
        {
            const uint32_t row = ringStart(y);
            const uint32_t nextRow = ringStart(y + 1);
            for (int x = 0; x < slices; ++x)
            {
                const uint32_t a = row + static_cast<uint32_t>(x);
                const uint32_t b = a + 1;
                const uint32_t c = nextRow + static_cast<uint32_t>(x);
                const uint32_t d = c + 1;
                indices.insert(indices.end(), {a, c, b, b, c, d});
            }
        }

        // Cap triangles get their own pole vertex. The pole U is the midpoint of
        // the triangle's longitude interval, preventing a shared U=0.5 pole from
        // dragging every cap triangle across half of the texture.
        const uint32_t firstRing = ringStart(1);
        const uint32_t lastRing = ringStart(stacks - 1);
        for (int x = 0; x < slices; ++x)
        {
            const float uMid = (static_cast<float>(x) + 0.5f) / static_cast<float>(slices);
            const float thetaMid = uMid * 6.283185307f;

            const uint32_t top = static_cast<uint32_t>(vertices.size());
            vertices.push_back({{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, makeSphereTangent(thetaMid), {uMid, 1.0f}});
            indices.insert(indices.end(), {
                top,
                firstRing + static_cast<uint32_t>(x),
                firstRing + static_cast<uint32_t>(x + 1)
            });

            const uint32_t bottom = static_cast<uint32_t>(vertices.size());
            vertices.push_back({{0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, makeSphereTangent(thetaMid), {uMid, 0.0f}});
            indices.insert(indices.end(), {
                lastRing + static_cast<uint32_t>(x),
                bottom,
                lastRing + static_cast<uint32_t>(x + 1)
            });
        }

        if (!buildMesh(vertices, indices, sphereMesh_))
        {
            error = L"Could not create the sphere preview mesh.";
            return false;
        }

        return true;
    }

    void UpdateMaterialCameraBuffer(const D3D11_VIEWPORT& vp)
    {
        if (!materialCameraBuffer_) return;

        using namespace DirectX;
        const float aspect = std::max(0.001f, vp.Width / std::max(1.0f, vp.Height));
        const float yaw = XMConvertToRadians(cameraYawDegrees_);
        const float pitch = XMConvertToRadians(cameraPitchDegrees_);
        const float radius = (previewMesh_ == PreviewMesh::Plane || previewMesh_ == PreviewMesh::Card) ? std::max(3.0f, cameraDistance_) : cameraDistance_;
        XMVECTOR target = XMVectorZero();
        XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -radius, 1.0f);
        const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
        eye = XMVector3TransformCoord(eye, rotation);
        XMVECTOR panOffset = XMVectorSet(cameraPanX_, cameraPanY_, 0.0f, 0.0f);
        panOffset = XMVector3TransformNormal(panOffset, rotation);
        target = XMVectorAdd(target, panOffset);
        eye = XMVectorAdd(eye, target);
        const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMMATRIX view = XMMatrixLookAtLH(eye, target, up);
        const float materialFov = XMConvertToRadians(45.0f);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(materialFov, aspect, 0.05f, 100.0f);
        XMMATRIX world = XMMatrixIdentity();
        if (previewMesh_ == PreviewMesh::Plane || previewMesh_ == PreviewMesh::Card)
            world = XMMatrixScaling(1.8f, 1.8f, 1.8f);
        const XMMATRIX wvp = world * view * proj;

        struct MaterialCameraData
        {
            DirectX::XMFLOAT4X4 worldViewProj;
            DirectX::XMFLOAT4X4 world;
            DirectX::XMFLOAT4 cameraPos;
            DirectX::XMFLOAT4 lightDir;
            DirectX::XMFLOAT4 uvTransform; // xy = tiling, zw = offset
        } data{};

        XMStoreFloat4x4(&data.worldViewProj, XMMatrixTranspose(wvp));
        XMStoreFloat4x4(&data.world, XMMatrixTranspose(world));
        DirectX::XMFLOAT3 eye3{};
        XMStoreFloat3(&eye3, eye);
        data.cameraPos = {eye3.x, eye3.y, eye3.z, 1.0f};
        const float ly = XMConvertToRadians(lightYawDegrees_);
        const float lp = XMConvertToRadians(lightPitchDegrees_);
        const float lx = std::cos(lp) * std::cos(ly);
        const float lyy = std::sin(lp);
        const float lz = std::cos(lp) * std::sin(ly);
        data.lightDir = {lx, lyy, lz, 0.0f};
        const bool directionalGeometry =
            skyShaderMode_ && !vertexOnlyShader_ &&
            (previewMode_ == PreviewMode::ForwardMaterial || previewMode_ == PreviewMode::DeferredGBuffer);
        data.uvTransform = {
            materialUvScaleU_,
            materialUvScaleV_,
            (directionalGeometry && previewMesh_ == PreviewMesh::Plane) ? 1.0f : 0.0f,
            0.0f
        };

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(materialCameraBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &data, sizeof(data));
            context_->Unmap(materialCameraBuffer_.Get(), 0);
        }
    }

    ID3D11ShaderResourceView* CurrentGBufferViewSRV(bool& isDepthView) const
    {
        isDepthView = false;
        auto slot = [&](size_t i) -> ID3D11ShaderResourceView* { return i < gbufferSRVs_.size() ? gbufferSRVs_[i].Get() : nullptr; };
        switch (gbufferView_)
        {
        case GBufferView::Depth: isDepthView = true; return materialDepthSRV_.Get();
        case GBufferView::RT1: return slot(1);
        case GBufferView::RT2: return slot(2);
        case GBufferView::RT3: return slot(3);
        case GBufferView::Albedo: return slot(0);
        case GBufferView::Normal: return slot(1);
        case GBufferView::Specular: return slot(2);
        case GBufferView::Gloss: return slot(3);
        case GBufferView::AO: return slot(3);
        case GBufferView::Emissive: return slot(3);
        case GBufferView::Final:
        case GBufferView::RT0:
        default: return slot(0);
        }
    }

    const PreviewMeshBuffers* CurrentPreviewMesh() const
    {
        // APE Match and Neutral/No Lighting use Treyarch's actual APE preview
        // geometry when it is available from the user's local BO3 Mod Tools
        // install. This preserves APE's authored UV seams/tiling instead of
        // approximating them with the Studio's procedural primitives.
        if (materialPreviewProfile_ != MaterialPreviewProfile::LookDev)
        {
            if (previewMesh_ == PreviewMesh::Sphere && HasApeReferenceMesh(PreviewMesh::Sphere)) return &apeSphereMesh_;
            if (previewMesh_ == PreviewMesh::Cube && HasApeReferenceMesh(PreviewMesh::Cube)) return &apeCubeMesh_;
            if (previewMesh_ == PreviewMesh::Plane && HasApeReferenceMesh(PreviewMesh::Plane)) return &apePlaneMesh_;
        }
        if (previewMesh_ == PreviewMesh::Cube) return &cubeMesh_;
        if (previewMesh_ == PreviewMesh::Plane) return &planeMesh_;
        if (previewMesh_ == PreviewMesh::Card) return &cardMesh_;
        if (previewMesh_ == PreviewMesh::Custom && customMesh_.vb) return &customMesh_;
        return &sphereMesh_;
    }

    void BuildMaterialShaderResources(std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT>& srvs)
    {
        srvs.fill(nullptr);
        bool reservesSceneSlot0 = false;
        bool reservesDepthSlot1 = false;
        for (const auto& resource : resources_)
        {
            ID3D11ShaderResourceView* fallback = NeutralForDimension(resource.dimension);
            const bool beginnerAlbedo = resource.name == "beginnerAlbedoMap";
            if (beginnerAlbedo) fallback = neutralSRV_.Get();
            else if (resource.name == "beginnerNormalMap") fallback = neutralNormalSRV_.Get();
            else if (resource.name == "beginnerSpecularMap") fallback = neutralSpecularSRV_.Get();
            else if (resource.name == "beginnerGlossMap") fallback = neutralGlossSRV_.Get();
            else if (resource.name == "beginnerAOMap") fallback = neutralSRV_.Get();
            else if (resource.name == "beginnerEmissiveMap") fallback = neutralBlackSRV_.Get();

            for (UINT n = 0; n < std::max(1u, resource.bindCount); ++n)
            {
                const UINT slot = resource.slot + n;
                if (slot >= srvs.size()) break;
                srvs[slot] = fallback;
            }
            if (resource.name == "frameBuffer" && resource.slot == 0 && Is2DLike(resource.dimension))
            {
                srvs[0] = sourceSRV_.Get();
                reservesSceneSlot0 = true;
            }
            else if (resource.slot == 0 && Is2DLike(resource.dimension) && !beginnerAlbedo)
            {
                srvs[0] = sourceSRV_.Get();
            }

            if (resource.name == "DepthSampler" && resource.slot == 1 && Is2DLike(resource.dimension))
            {
                srvs[1] = ActivePreviewDepthSRV();
                reservesDepthSlot1 = true;
            }
            else if (resource.slot == 1 && Is2DLike(resource.dimension)) srvs[1] = ActivePreviewDepthSRV();
        }
        if (!srvs[0]) srvs[0] = sourceSRV_.Get();
        if (!srvs[1]) srvs[1] = ActivePreviewDepthSRV();
        if (vertexOnlyShader_)
        {
            if (!srvs[2]) srvs[2] = neutralNormalSRV_.Get();
            if (!srvs[4]) srvs[4] = neutralSpecularSRV_.Get();
            if (!srvs[5]) srvs[5] = neutralGlossSRV_.Get();
            if (!srvs[6]) srvs[6] = neutralSRV_.Get();
            if (!srvs[7]) srvs[7] = neutralBlackSRV_.Get();
        }

        for (int i = 0; i < kMaterialTextureSlotCount; ++i)
        {
            ID3D11ShaderResourceView* srv = materialTextureSRVs_[static_cast<size_t>(i)].Get();
            if (!srv) continue;
            const UINT bindSlot = materialTextureBindings_[static_cast<size_t>(i)];
            // A scene-sampling Advanced material may own t0/t1 as live scene/depth
            // inputs. Do not let an unrelated image assignment replace them.
            if ((bindSlot == 0 && reservesSceneSlot0) ||
                (bindSlot == 1 && reservesDepthSlot1))
                continue;
            if (bindSlot < srvs.size())
                srvs[bindSlot] = srv;
        }
    }

    void SetupMaterialGeometryPass(const D3D11_VIEWPORT& vp)
    {
        const PreviewMeshBuffers* mesh = CurrentPreviewMesh();
        if (!mesh || !mesh->vb || !mesh->ib || mesh->indexCount == 0) return;

        UINT stride = sizeof(MaterialVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = mesh->vb.Get();
        context_->IASetInputLayout(vertexOnlyShader_ ? userMaterialInputLayout_.Get() : materialInputLayout_.Get());
        context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        context_->IASetIndexBuffer(mesh->ib.Get(), DXGI_FORMAT_R32_UINT, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->RSSetState((wireframe_ && materialWireframeRasterizerState_) ? materialWireframeRasterizerState_.Get() : materialRasterizerState_.Get());
        context_->RSSetViewports(1, &vp);
        const bool directionalGeometry = skyShaderMode_ && !vertexOnlyShader_;
        context_->VSSetShader(
            vertexOnlyShader_
                ? userVertexShader_.Get()
                : (adaptedMaterialShader_ ? adaptedMaterialVertexShader_.Get()
                   : (directionalGeometry ? directionalMaterialVertexShader_.Get() : materialVertexShader_.Get())),
            nullptr, 0);
        UpdateMaterialCameraBuffer(vp);
        ID3D11Buffer* materialCB = materialCameraBuffer_.Get();
        context_->VSSetConstantBuffers(13, 1, &materialCB);
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        if (vertexOnlyShader_)
        {
            UpdateDeferredLightBuffer();
            ID3D11Buffer* fallbackLightCB = deferredLightBuffer_.Get();
            context_->PSSetConstantBuffers(13, 1, &fallbackLightCB);
        }

        // Geometry UV tiling must wrap. PostFX intentionally keeps the clamp
        // sampler, but material meshes use a dedicated wrap sampler so U/V values
        // above 1.0 actually tile instead of smearing the border texels.
        std::array<ID3D11SamplerState*, 16> samplers{};
        ID3D11SamplerState* geometrySampler = materialWrapSampler_.Get() ? materialWrapSampler_.Get() : sampler_.Get();
        samplers.fill(geometrySampler);
        if (materialColorSampler_.Get()) samplers[0] = materialColorSampler_.Get();
        context_->PSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());
        if (vertexOnlyShader_)
            context_->VSSetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());

        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> srvs{};
        BuildMaterialShaderResources(srvs);
        context_->PSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
        if (vertexOnlyShader_)
            context_->VSSetShaderResources(0, static_cast<UINT>(srvs.size()), srvs.data());
        UpdateConstantBuffers(vp);
    }

    void CleanupAfterMaterialDraw()
    {
        std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> nullSRV{};
        context_->PSSetShaderResources(0, static_cast<UINT>(nullSRV.size()), nullSRV.data());
        context_->VSSetShaderResources(0, static_cast<UINT>(nullSRV.size()), nullSRV.data());
    }

    void BlitSingleShaderResource(ID3D11ShaderResourceView* blit, bool depthView, const D3D11_VIEWPORT& vp)
    {
        if (!blit) return;
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &vp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(blitVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(depthView ? blitDepthPixelShader_.Get() : blitPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, &blit);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* nullBlit = nullptr;
        context_->PSSetShaderResources(0, 1, &nullBlit);
    }

    void UpdateEnvironmentCameraBuffer(const D3D11_VIEWPORT& vp)
    {
        if (!cameraVSBuffer_) return;
        std::array<float, 20> bytes{};

        if (previewMode_ == PreviewMode::ForwardMaterial || previewMode_ == PreviewMode::DeferredGBuffer)
        {
            using namespace DirectX;
            const float yaw = XMConvertToRadians(cameraYawDegrees_);
            const float pitch = XMConvertToRadians(cameraPitchDegrees_);
            const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
            XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
            forward = XMVector3Normalize(forward);
            XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
            if (XMVectorGetX(XMVector3LengthSq(right)) < 0.000001f)
                right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
            XMFLOAT3 rf{}, uf{}, ff{};
            XMStoreFloat3(&rf, right); XMStoreFloat3(&uf, up); XMStoreFloat3(&ff, forward);
            const float aspect = std::max(0.01f, vp.Width / std::max(1.0f, vp.Height));
            const float tanHalfFov = std::tan(XMConvertToRadians(45.0f) * 0.5f);
            const std::array<float,4> r{rf.x, rf.y, rf.z, 0.0f};
            const std::array<float,4> u{uf.x, uf.y, uf.z, 0.0f};
            const std::array<float,4> f{ff.x, ff.y, ff.z, 0.0f};
            const std::array<float,4> params{aspect, tanHalfFov, cameraYawDegrees_, cameraPitchDegrees_};
            std::copy(r.begin(), r.end(), bytes.begin());
            std::copy(u.begin(), u.end(), bytes.begin() + 4);
            std::copy(f.begin(), f.end(), bytes.begin() + 8);
            std::copy(params.begin(), params.end(), bytes.begin() + 12);
            DirectX::XMFLOAT3 eyePos{};
            const float radius = (previewMesh_ == PreviewMesh::Plane || previewMesh_ == PreviewMesh::Card) ? std::max(3.0f, cameraDistance_) : cameraDistance_;
            DirectX::XMVECTOR eye = DirectX::XMVectorSet(0.0f, 0.0f, -radius, 1.0f);
            eye = DirectX::XMVector3TransformCoord(eye, rotation);
            DirectX::XMVECTOR panOffset = DirectX::XMVectorSet(cameraPanX_, cameraPanY_, 0.0f, 0.0f);
            panOffset = DirectX::XMVector3TransformNormal(panOffset, rotation);
            eye = DirectX::XMVectorAdd(eye, panOffset);
            DirectX::XMStoreFloat3(&eyePos, eye);
            const std::array<float,4> cameraPos{eyePos.x, eyePos.y, eyePos.z, 1.0f};
            std::copy(cameraPos.begin(), cameraPos.end(), bytes.begin() + 16);
        }
        else
        {
            const CameraFrameData camera = BuildCameraFrame(vp);
            std::copy(camera.right.begin(), camera.right.end(), bytes.begin());
            std::copy(camera.up.begin(), camera.up.end(), bytes.begin() + 4);
            std::copy(camera.forward.begin(), camera.forward.end(), bytes.begin() + 8);
            std::copy(camera.params.begin(), camera.params.end(), bytes.begin() + 12);
        }

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(cameraVSBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, bytes.data(), sizeof(bytes));
            context_->Unmap(cameraVSBuffer_.Get(), 0);
        }
        ID3D11Buffer* buffer = cameraVSBuffer_.Get();
        context_->VSSetConstantBuffers(0, 1, &buffer);
        context_->PSSetConstantBuffers(0, 1, &buffer);
    }

    void RenderEnvironmentBackground(const D3D11_VIEWPORT& vp)
    {
        if (!environmentEnabled_ || !environmentSRV_ || !environmentPixelShader_) return;
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &vp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(skyVertexShader_.Get(), nullptr, 0);
        UpdateEnvironmentCameraBuffer(vp);
        UpdateDeferredLightBuffer();
        ID3D11Buffer* environmentSettings = deferredLightBuffer_.Get();
        context_->PSSetConstantBuffers(13, 1, &environmentSettings);
        context_->PSSetShader(environmentPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        ID3D11ShaderResourceView* env = environmentSRV_.Get();
        context_->PSSetShaderResources(0, 1, &env);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* nullEnv = nullptr;
        context_->PSSetShaderResources(0, 1, &nullEnv);
    }

    void RenderForwardMaterialPreview()
    {
        if (!pixelShader_ || !materialVertexShader_ || !directionalMaterialVertexShader_ || !materialInputLayout_ || !renderTarget_ || !materialDepthDSV_) return;

        const float clearColor[4] = {backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], 1.0f};
        context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
        context_->ClearDepthStencilView(materialDepthDSV_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(std::max<LONG>(1, width_));
        vp.Height = static_cast<float>(std::max<LONG>(1, height_));
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        lastViewportWidth_ = static_cast<UINT>(std::max(1.0f, vp.Width));
        lastViewportHeight_ = static_cast<UINT>(std::max(1.0f, vp.Height));

        if (materialPreviewProfile_ != MaterialPreviewProfile::Neutral)
            RenderEnvironmentBackground(vp);
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), materialDepthDSV_.Get());
        SetupMaterialGeometryPass(vp);
        const PreviewMeshBuffers* mesh = CurrentPreviewMesh();
        if (!mesh || !mesh->indexCount) return;
        BeginGpuTimer();
        context_->DrawIndexed(mesh->indexCount, 0, 0);
        EndGpuTimer();
        CleanupAfterMaterialDraw();
    }

    void RenderDeferredMaterialPreview()
    {
        if (!pixelShader_ || !materialVertexShader_ || !directionalMaterialVertexShader_ || !materialInputLayout_ || !renderTarget_) return;
        if (!gbufferRTVs_[0] || !materialDepthDSV_) return;

        const float clearColor[4] = {backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], 1.0f};
        const float clearGBuffer[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
        for (auto& rtv : gbufferRTVs_) if (rtv) context_->ClearRenderTargetView(rtv.Get(), clearGBuffer);
        context_->ClearDepthStencilView(materialDepthDSV_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(std::max<LONG>(1, width_));
        vp.Height = static_cast<float>(std::max<LONG>(1, height_));
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        lastViewportWidth_ = static_cast<UINT>(std::max(1.0f, vp.Width));
        lastViewportHeight_ = static_cast<UINT>(std::max(1.0f, vp.Height));

        ID3D11RenderTargetView* mrt[4] = { gbufferRTVs_[0].Get(), gbufferRTVs_[1].Get(), gbufferRTVs_[2].Get(), gbufferRTVs_[3].Get() };
        context_->OMSetRenderTargets(4, mrt, materialDepthDSV_.Get());
        SetupMaterialGeometryPass(vp);
        const PreviewMeshBuffers* mesh = CurrentPreviewMesh();
        if (!mesh || !mesh->indexCount) return;
        BeginGpuTimer();
        context_->DrawIndexed(mesh->indexCount, 0, 0);
        EndGpuTimer();
        CleanupAfterMaterialDraw();

        // Final Lit and every GBuffer inspector mode now go through the same
        // BO3-aware compositor. Raw RT modes remain raw; semantic views decode
        // BO3's packed NormalGloss/ReflectanceOcclusion fields correctly.
        context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
        context_->RSSetViewports(1, &vp);
        context_->IASetInputLayout(nullptr);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(blitVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(deferredLightPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        UpdateDeferredLightBuffer();
        UpdateEnvironmentCameraBuffer(vp);
        ID3D11Buffer* deferredCB = deferredLightBuffer_.Get();
        context_->PSSetConstantBuffers(13, 1, &deferredCB);
        ID3D11ShaderResourceView* composeSrvs[7] = {
            gbufferSRVs_[0].Get(), gbufferSRVs_[1].Get(), gbufferSRVs_[2].Get(), gbufferSRVs_[3].Get(), materialDepthSRV_.Get(),
            environmentEnabled_ ? environmentSRV_.Get() : neutralSRV_.Get(),
            materialTextureSRVs_[0].Get() ? materialTextureSRVs_[0].Get() : neutralSRV_.Get()
        };
        context_->PSSetShaderResources(0, 7, composeSrvs);
        context_->Draw(3, 0);
        ID3D11ShaderResourceView* nullCompose[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        context_->PSSetShaderResources(0, 7, nullCompose);
    }

    bool CreateBuiltInVertexShaders(std::wstring& error)
    {
        // Normal fullscreen/post-FX path. TEXCOORD0 is UV. We also provide a
        // harmless TEXCOORD1 so shaders with a second interpolator can link.
        static const char* postFxSource = R"(
struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
};
VS_OUT vs_main(uint id : SV_VertexID)
{
    VS_OUT o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    o.texcoord0 = float4(uv, 0.0, 1.0);
    o.texcoord1 = float4(0.0, 0.0, 0.0, 0.0);
    return o;
}
)";

        static const char* skySource = R"(
cbuffer PreviewCamera : register(b0)
{
    float4 previewCameraRight;
    float4 previewCameraUp;
    float4 previewCameraForward;
    float4 previewCameraParams; // x = aspect, y = tan(fov/2)
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 skyDirection : TEXCOORD0;
    float4 fogDirection : TEXCOORD1;
};
VS_OUT vs_main(uint id : SV_VertexID)
{
    VS_OUT o;
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    o.position = float4(ndc, 0.0, 1.0);

    float3 ray =
        previewCameraForward.xyz +
        previewCameraRight.xyz * (ndc.x * previewCameraParams.x * previewCameraParams.y) +
        previewCameraUp.xyz * (ndc.y * previewCameraParams.y);

    o.skyDirection = float4(ray, 0.0);
    o.fogDirection = float4(ray, 0.0);
    return o;
}
)";

        static const char* materialSource = R"(
cbuffer PreviewMaterialCamera : register(b13)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 previewCameraPos;
    float4 previewLightDir;
    float4 previewUvTransform; // xy = tiling, zw = offset
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
    float4 texcoord2 : TEXCOORD2;
    float4 texcoord3 : TEXCOORD3;
    float4 texcoord4 : TEXCOORD4;
    float4 texcoord5 : TEXCOORD5;
    float4 texcoord6 : TEXCOORD6;
};

VS_OUT vs_main(VS_IN i)
{
    VS_OUT o;
    float4 worldPos = mul(float4(i.position, 1.0), world);
    float3 worldNormal = normalize(mul(float4(i.normal, 0.0), world).xyz);
    float3 worldTangent = normalize(mul(float4(i.tangent.xyz, 0.0), world).xyz);
    float tangentHandedness = (abs(i.tangent.w) < 0.0001) ? 1.0 : i.tangent.w;
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent) * tangentHandedness);

    o.position = mul(float4(i.position, 1.0), worldViewProj);
    o.texcoord0 = float4(i.texcoord.xy * previewUvTransform.xy + previewUvTransform.zw, 0.0, 1.0);
    o.texcoord1 = worldPos;
    o.texcoord2 = float4(worldNormal, 1.0);
    o.texcoord3 = float4(worldTangent, tangentHandedness);
    o.texcoord4 = float4(worldBitangent, 1.0);
    // Raw converted GLSL materials use local/object position for the seamless
    // closed-surface triplanar projection. Supplying it explicitly avoids any
    // dependence on UV seams, pole vertices, or world-space object placement.
    o.texcoord5 = float4(i.position, 1.0);
    o.texcoord6 = float4(normalize(i.normal), 0.0);
    return o;
}
)";

        // Directional material path for BO3 sky/environment pixel shaders when
        // the user previews them on Sphere / Cube / Plane. A BO3 sky PS expects
        // TEXCOORD0/1 to be 3D directions, not ordinary 2D mesh UVs. Feeding UV
        // coordinates into skyDirection was the cause of the pole pinching and
        // triangular streaks seen on the sphere/cube preview.
        static const char* directionalMaterialSource = R"(
cbuffer PreviewMaterialCamera : register(b13)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 previewCameraPos;
    float4 previewLightDir;
    float4 previewUvTransform; // xy = equirect tiling; z = plane projection flag
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
    float4 texcoord2 : TEXCOORD2;
    float4 texcoord3 : TEXCOORD3;
    float4 texcoord4 : TEXCOORD4;
};

static const float PREVIEW_PI = 3.14159265358979323846;
static const float PREVIEW_TWO_PI = 6.28318530717958647692;

float2 PreviewDirectionToEquirect(float3 direction)
{
    float3 d = normalize(direction);
    float u = atan2(d.y, d.x) / PREVIEW_TWO_PI + 0.5;
    float v = 0.5 - asin(clamp(d.z, -1.0, 1.0)) / PREVIEW_PI;
    return float2(u, v);
}

float3 PreviewEquirectToDirection(float2 uv)
{
    float longitude = (uv.x - 0.5) * PREVIEW_TWO_PI;
    float latitude = (0.5 - uv.y) * PREVIEW_PI;
    float cosLatitude = cos(latitude);
    return float3(
        cosLatitude * cos(longitude),
        cosLatitude * sin(longitude),
        sin(latitude)
    );
}

float PreviewTileAxis(float value, float scale, float preserveEndPoint)
{
    float safeScale = max(abs(scale), 0.0001);
    if (preserveEndPoint > 0.5 && abs(safeScale - 1.0) < 0.0001)
        return saturate(value);
    return frac(value * safeScale);
}

VS_OUT vs_main(VS_IN i)
{
    VS_OUT o;
    float4 worldPos4 = mul(float4(i.position, 1.0), world);
    float3 worldNormal = normalize(mul(float4(i.normal, 0.0), world).xyz);
    float3 worldTangent = normalize(mul(float4(i.tangent.xyz, 0.0), world).xyz);
    float tangentHandedness = (abs(i.tangent.w) < 0.0001) ? 1.0 : i.tangent.w;
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent) * tangentHandedness);

    // Material preview meshes are Y-up while BO3 skyDirection is Z-up.
    float3 direction;
    if (previewUvTransform.z > 0.5)
    {
        // Plane preview: treat the plane as an equirectangular canvas.
        direction = PreviewEquirectToDirection(i.texcoord);
    }
    else
    {
        // Sphere/cube preview: project from object center so every face shares
        // one continuous spherical direction field instead of six unrelated UVs.
        direction = normalize(float3(worldPos4.x, worldPos4.z, worldPos4.y));
    }

    float2 environmentUv = PreviewDirectionToEquirect(direction);
    environmentUv.x = PreviewTileAxis(environmentUv.x, previewUvTransform.x, 0.0);
    environmentUv.y = PreviewTileAxis(environmentUv.y, previewUvTransform.y, 1.0);
    direction = PreviewEquirectToDirection(environmentUv);

    o.position = mul(float4(i.position, 1.0), worldViewProj);
    o.texcoord0 = float4(direction, 0.0);
    o.texcoord1 = float4(direction, 0.0);
    o.texcoord2 = float4(worldNormal, 1.0);
    o.texcoord3 = float4(worldTangent, tangentHandedness);
    o.texcoord4 = float4(worldBitangent, 1.0);
    return o;
}
)";

        // The BO3 material adapter emits a richer pixel interface than the
        // generic material preview. This in-memory VS matches that interface so
        // temporary Material preview uses real geometry fields instead of
        // receiving zeros or mismatched TEXCOORD meanings. BO3 validation still
        // compiles and reflects the adapter's authored/export VS separately.
        static const char* adaptedMaterialSource = R"(
cbuffer PreviewMaterialCamera : register(b13)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 previewCameraPos;
    float4 previewLightDir;
    float4 previewUvTransform;
};

struct VS_IN
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float color : COLOR1;
    float2 texCoords : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 biTangent : TEXCOORD3;
    float3 objectPosition : TEXCOORD4;
    float3 worldPosition : TEXCOORD5;
    float3 viewDirWorld : TEXCOORD6;
    uint instance : TEXCOORD7;
    float3 objectNormal : TEXCOORD9;
};

VS_OUT vs_main(VS_IN input)
{
    VS_OUT output;
    float4 worldPosition = mul(float4(input.position, 1.0), world);
    float3 worldNormal = normalize(mul(float4(input.normal, 0.0), world).xyz);
    float3 worldTangent = normalize(mul(float4(input.tangent.xyz, 0.0), world).xyz);
    float handedness = abs(input.tangent.w) < 0.0001 ? 1.0 : input.tangent.w;
    output.position = mul(float4(input.position, 1.0), worldViewProj);
    output.color = 1.0;
    output.texCoords = input.texcoord * previewUvTransform.xy + previewUvTransform.zw;
    output.normal = worldNormal;
    output.tangent = worldTangent;
    output.biTangent = normalize(cross(worldNormal, worldTangent) * handedness);
    output.objectPosition = input.position;
    output.objectNormal = normalize(input.normal);
    output.worldPosition = worldPosition.xyz;
    output.viewDirWorld = normalize(previewCameraPos.xyz - worldPosition.xyz);
    output.instance = 0;
    return output;
}
)";

        static const char* blitPsSource = R"(
Texture2D previewTexture : register(t0);
SamplerState previewSampler : register(s0);

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
};

float4 ps_main(VS_OUT i) : SV_Target0
{
    return previewTexture.Sample(previewSampler, i.texcoord0.xy);
}
)";

        static const char* runtimeSceneEncodePsSource = R"(
Texture2D previewTexture : register(t0);
SamplerState previewSampler : register(s0);
cbuffer PreviewRuntimeSceneParams : register(b0)
{
    // x = 1 for a true scene-linear HDR source (EXR), 0 for LDR sRGB.
    // y = explicit BO3 runtime scene exposure multiplier (2^EV).
    float4 previewRuntimeSceneParams;
};
struct VS_OUT { float4 position : SV_Position; float4 texcoord0 : TEXCOORD0; float4 texcoord1 : TEXCOORD1; };

float3 PreviewSrgbToLinear(float3 srgbColor)
{
    srgbColor = saturate(srgbColor);
    const float3 low = srgbColor / 12.92;
    const float3 high = pow((srgbColor + 0.055) / 1.055, 2.4);
    const float3 useHigh = step(float3(0.04045, 0.04045, 0.04045), srgbColor);
    return lerp(low, high, useHigh);
}

float4 ps_main(VS_OUT i) : SV_Target0
{
    float4 c = previewTexture.Sample(previewSampler, i.texcoord0.xy);

    // True EXR input is already scene-linear and can contain values above 1.0.
    // Ordinary PNG/JPEG input is only an LDR approximation, so decode it from
    // sRGB first. The explicit scene exposure is applied before BO3's 32768
    // postfx storage scale, matching the domain seen by PostFx_NormalizeColor.
    float3 linearScene = previewRuntimeSceneParams.x > 0.5
        ? max(c.rgb, 0.0)
        : PreviewSrgbToLinear(c.rgb);
    linearScene *= max(previewRuntimeSceneParams.y, 0.000001);
    return float4(linearScene * 32768.0, c.a);
}
)";

        static const char* runtimeSceneDecodePsSource = R"(
Texture2D previewTexture : register(t0);
SamplerState previewSampler : register(s0);
struct VS_OUT { float4 position : SV_Position; float4 texcoord0 : TEXCOORD0; float4 texcoord1 : TEXCOORD1; };

float3 PreviewLinearToSrgb(float3 linearColor)
{
    linearColor = max(linearColor, 0.0);
    const float3 low = linearColor * 12.92;
    const float3 high = 1.055 * pow(linearColor, 1.0 / 2.4) - 0.055;
    const float3 useHigh = step(float3(0.0031308, 0.0031308, 0.0031308), linearColor);
    return lerp(low, high, useHigh);
}

float4 ps_main(VS_OUT i) : SV_Target0
{
    float4 c = previewTexture.Sample(previewSampler, i.texcoord0.xy);

    // BO3's postfx shader output is still scene-linear after undoing the 32768
    // runtime scale. The real game reaches a display transfer before the user
    // sees it; writing those linear values directly into our UNORM swap chain
    // makes the Previewer too dark and overly saturated. Encode to sRGB here so
    // the runtime preview is compared in the same display domain as a BO3 capture.
    const float3 linearDisplay = c.rgb / 32768.0;
    return float4(saturate(PreviewLinearToSrgb(linearDisplay)), c.a);
}
)";

        static const char* blitDepthPsSource = R"(
Texture2D previewTexture : register(t0);
SamplerState previewSampler : register(s0);

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
};

float4 ps_main(VS_OUT i) : SV_Target0
{
    float d = saturate(previewTexture.Sample(previewSampler, i.texcoord0.xy).r);
    float v = 1.0 - pow(saturate(d), 0.22);
    return float4(v, v, v, 1.0);
}
)";

        static const char* environmentPsSource = R"(
Texture2D previewEnvironment : register(t0);
SamplerState previewSampler : register(s0);
cbuffer PreviewEnvironmentSettings : register(b13)
{
    float4 previewLightDirIntensity;
    float4 previewAmbientShadow;
    float4 previewEnvironmentAmbient;
    float4 previewLightColorFulbright;
    float4 previewBackgroundColor;
    float4 previewLookdevSettings;
    float4 previewDebugSettings;
    float4 previewApeSettings;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 skyDirection : TEXCOORD0;
    float4 fogDirection : TEXCOORD1;
};

float2 DirectionToEquirect(float3 direction)
{
    float3 d = normalize(direction);
    // APE/BO3's asset-preview environment uses the opposite horizontal
    // handedness from the Studio camera frame. Keep this isolated to APE Match
    // so Look Dev and user-authored environment orientation remain unchanged.
    int profile = (int)(previewDebugSettings.y + 0.5);
    if (profile == 0)
        d.x = -d.x;
    float angle = previewApeSettings.x;
    float s = sin(angle), c = cos(angle);
    d.xz = float2(d.x * c - d.z * s, d.x * s + d.z * c);
    float u = atan2(d.z, d.x) * 0.15915494309189535 + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) * 0.3183098861837907;
    return float2(frac(u), saturate(v));
}

float3 ApplyEnvironmentDisplay(float3 color)
{
    color = max(color, 0.0);
    int profile = (int)(previewDebugSettings.y + 0.5);
    if (profile == 2) return previewBackgroundColor.rgb;
    color *= exp2(previewLookdevSettings.x);
    int mode = (int)(previewLookdevSettings.y + 0.5);
    if (mode == 1)
        color = color / (1.0 + color);
    else if (mode == 2 || profile == 0)
    {
        const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));
    }
    return color;
}

float4 ps_main(VS_OUT i) : SV_Target0
{
    float3 env = previewEnvironment.Sample(previewSampler, DirectionToEquirect(i.skyDirection.xyz)).rgb;
    return float4(ApplyEnvironmentDisplay(env), 1.0);
}
)";

        static const char* vertexPreviewPsSource = R"(
Texture2D previewAlbedo : register(t0);
Texture2D previewNormal : register(t2);
Texture2D previewSpecular : register(t4);
Texture2D previewGloss : register(t5);
Texture2D previewAO : register(t6);
Texture2D previewEmissive : register(t7);
SamplerState previewSampler : register(s0);
cbuffer PreviewFallbackLighting : register(b13)
{
    float4 previewLightDirIntensity;
    float4 previewAmbientShadow;
    float4 previewEnvironmentAmbient;
    float4 previewLightColorFulbright;
};

struct VS_OUT
{
    float4 position : SV_Position;
    float2 texcoord0 : TEXCOORD0;
};

struct PS_OUT
{
    float4 rt0 : SV_Target0;
    float4 rt1 : SV_Target1;
    float4 rt2 : SV_Target2;
    float4 rt3 : SV_Target3;
};

PS_OUT ps_main(VS_OUT i)
{
    PS_OUT o;
    float2 uv = i.texcoord0.xy;
    float4 albedo = previewAlbedo.Sample(previewSampler, uv);
    float3 sampledNormal = previewNormal.Sample(previewSampler, uv).xyz;
    float3 n = normalize(sampledNormal * 2.0 - 1.0);
    if (dot(n,n) < 0.01) n = float3(0,0,1);
    float3 specular = previewSpecular.Sample(previewSampler, uv).rgb;
    float gloss = previewGloss.Sample(previewSampler, uv).r;
    float ao = previewAO.Sample(previewSampler, uv).r;
    float3 emissive = previewEmissive.Sample(previewSampler, uv).rgb;

    // In Forward Material mode the preview fallback provides a directly lit
    // color. In Deferred/GBuffer mode RT0 remains the albedo GBuffer target.
    if (previewEnvironmentAmbient.w > 0.5)
    {
        if (previewLightColorFulbright.w > 0.5)
        {
            o.rt0 = albedo;
        }
        else
        {
            float3 L = normalize(previewLightDirIntensity.xyz);
            float ndotl = saturate(dot(n, L));
            float3 ambient = previewEnvironmentAmbient.rgb * previewAmbientShadow.x * ao;
            float3 direct = previewLightColorFulbright.rgb * ndotl * previewLightDirIntensity.w;
            float3 c = albedo.rgb * (ambient + direct) + emissive;
            o.rt0 = float4(c / (1.0 + c), albedo.a);
        }
    }
    else
    {
        o.rt0 = float4(albedo.rgb, albedo.a);
    }
    // Write the same packed normal/gloss and reflectance/occlusion layout that
    // BO3's GBufferPixelOutput uses. Keeping fallback/example shaders on the same
    // contract prevents the preview compositor from needing two incompatible RT
    // interpretations.
    float3 packedNormalInput = n;
    float halfSum = (n.x + n.y + n.z) * 0.5;
    float3 directionTest = n - halfSum;
    float maxComponent = max(halfSum, max(directionTest.x, max(directionTest.y, directionTest.z)));
    int normalDirection = 0;
    if (directionTest.x == maxComponent) { packedNormalInput *= float3( 1.0,-1.0,-1.0); normalDirection = 1; }
    else if (directionTest.y == maxComponent) { packedNormalInput *= float3(-1.0, 1.0,-1.0); normalDirection = 2; }
    else if (directionTest.z == maxComponent) { packedNormalInput *= float3(-1.0,-1.0, 1.0); normalDirection = 3; }

    float3 basis;
    basis.z = packedNormalInput.x + packedNormalInput.y + packedNormalInput.z;
    basis.x = packedNormalInput.x - 2.0 * packedNormalInput.y + packedNormalInput.z;
    basis.y = packedNormalInput.z - packedNormalInput.x;
    basis *= float3(0.408248, 0.707107, 0.577350);
    float packScale = rsqrt(abs(basis.z) + 1.0);
    float2 packedXY = basis.xy * packScale * 0.588235 + 0.5;
    float packedGloss = saturate(gloss) * 0.49755621 + 0.00146627566;

    o.rt1 = float4(packedXY, packedGloss, normalDirection * (1.0 / 3.0));
    float scalarSpec = saturate(max(specular.r, max(specular.g, specular.b)));
    o.rt2 = float4(max(0.04, scalarSpec), 0.5, saturate(ao), 1.0 / 3.0);
    // RT3 is preview-only here so legacy vertex-only examples can still show
    // emissive. Real opaque BO3 GBuffer materials normally leave it unused.
    o.rt3 = float4(emissive, saturate(ao));
    return o;
}
)";

        static const char* deferredLightPsSource = R"(
Texture2D gbuffer0 : register(t0);
Texture2D gbuffer1 : register(t1);
Texture2D gbuffer2 : register(t2);
Texture2D gbuffer3 : register(t3);
Texture2D depthTexture : register(t4);
Texture2D previewEnvironment : register(t5);
Texture2D previewMaterialAlbedoInput : register(t6);
SamplerState previewSampler : register(s0);

cbuffer PreviewCamera : register(b0)
{
    float4 previewCameraRight;
    float4 previewCameraUp;
    float4 previewCameraForward;
    float4 previewCameraParams;
    float4 previewCameraPosition;
};
cbuffer PreviewDeferredLight : register(b13)
{
    float4 previewLightDirIntensity;
    float4 previewAmbientShadow;       // x ambient, y shadow, z env affects light, w environment enabled
    float4 previewEnvironmentAmbient;  // rgb environment ambient tint, w = forward fallback marker
    float4 previewLightColorFulbright; // rgb sun color, w fulbright
    float4 previewBackgroundColor;     // preview clear/background color
    float4 previewLookdevSettings;     // x exposure EV, y tone map, z ground, w contact shadow
    float4 previewDebugSettings;       // x = GBufferView enum, y = MaterialPreviewProfile
    float4 previewApeSettings;         // x = environment yaw rotation in radians
};

struct VS_OUT
{
    float4 position : SV_Position;
    float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1;
};

// BO3 does not store world normals in RT1 as normal * 0.5 + 0.5.
// GBuffer_PackNormal() projects the normal into an orthonormal tetrahedral
// basis, stores two coordinates, and puts the selected tetrahedron direction
// in NormalGloss.w. Decode that exact contract here so the lookdev viewport
// follows the same GBuffer representation as the exported BO3 material.
float3 DecodeBo3GBufferNormal(float4 normalGloss)
{
    float2 q = (normalGloss.xy - 0.5) / 0.588235;
    float r2 = saturate(dot(q, q));
    float c = max(0.0, 1.0 - r2);
    float scale = sqrt(c + 1.0);
    float a = q.x * scale;
    float b = q.y * scale;

    const float invSqrt6 = 0.4082482904638631;
    const float invSqrt2 = 0.7071067811865475;
    const float invSqrt3 = 0.5773502691896258;
    float3 n = float3(
        a * invSqrt6 - b * invSqrt2 + c * invSqrt3,
       -2.0 * a * invSqrt6              + c * invSqrt3,
        a * invSqrt6 + b * invSqrt2 + c * invSqrt3);

    int direction = (int)round(saturate(normalGloss.w) * 3.0);
    if (direction == 1)      n *= float3( 1.0, -1.0, -1.0);
    else if (direction == 2) n *= float3(-1.0,  1.0, -1.0);
    else if (direction == 3) n *= float3(-1.0, -1.0,  1.0);

    float len2 = dot(n, n);
    return len2 > 1e-8 ? n * rsqrt(len2) : float3(0.0, 0.0, 1.0);
}

float DecodeBo3Gloss(float packedGloss)
{
    // For the flat tangent-space normal used by generated procedural materials,
    // GBuffer_PackGloss reduces to a normalized 0..17 gloss scale followed by
    // BO3's 0.49755621 range/offset. Recover the normalized lookdev value.
    return saturate((packedGloss - 0.00146627566) / 0.49755621);
}

float2 DirectionToEquirect(float3 direction)
{
    float3 d = normalize(direction);
    // APE/BO3's asset-preview environment uses the opposite horizontal
    // handedness from the Studio camera frame. Keep this isolated to APE Match
    // so Look Dev and user-authored environment orientation remain unchanged.
    int profile = (int)(previewDebugSettings.y + 0.5);
    if (profile == 0)
        d.x = -d.x;
    float angle = previewApeSettings.x;
    float s = sin(angle), c = cos(angle);
    d.xz = float2(d.x * c - d.z * s, d.x * s + d.z * c);
    float u = atan2(d.z, d.x) * 0.15915494309189535 + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) * 0.3183098861837907;
    return float2(frac(u), saturate(v));
}

float3 EnvironmentAt(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 ray = previewCameraForward.xyz +
                 previewCameraRight.xyz * (ndc.x * previewCameraParams.x * previewCameraParams.y) +
                 previewCameraUp.xyz * (ndc.y * previewCameraParams.y);
    return previewEnvironment.Sample(previewSampler, DirectionToEquirect(ray)).rgb;
}

float LinearToDisplay1(float x)
{
    x = max(x, 0.0);
    return x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 LinearToDisplay(float3 color)
{
    return float3(LinearToDisplay1(color.r), LinearToDisplay1(color.g), LinearToDisplay1(color.b));
}

float3 ApplyLookdev(float3 color)
{
    int profile = (int)(previewDebugSettings.y + 0.5);
    color = max(color, 0.0);

    // APE's No Lighting path is diagnostic, but its diffuse texture is still
    // sampled from an sRGB resource into linear shader space. Encode the final
    // linear material response for the UNORM desktop swapchain so a raw color
    // map visually matches APE instead of appearing artificially dark.
    if (profile == 2)
        return saturate(LinearToDisplay(color));

    color *= exp2(previewLookdevSettings.x);
    int mode = (int)(previewLookdevSettings.y + 0.5);
    if (mode == 1)
        color = color / (1.0 + color); // Reinhard
    else if (mode == 2 || profile == 0)
    {
        // APE Match currently uses the same compact shoulder/toe curve while
        // keeping the recovered SSI exposure and HDR environment separate.
        // This is deliberately isolated so screenshot calibration can refine
        // the display transform without changing Look Dev.
        const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));
    }

    // The swapchain is R8G8B8A8_UNORM rather than *_SRGB. APE/TOOLSGFX output
    // is display-referred, so APE Match must explicitly apply the display OETF.
    // Preserve legacy Look Dev output for now; this correction is parity-scoped.
    if (profile == 0)
        color = LinearToDisplay(color);
    return saturate(color);
}

float3 EnvironmentDirection(float3 direction)
{
    return previewEnvironment.Sample(previewSampler, DirectionToEquirect(direction)).rgb;
}

float4 ps_main(VS_OUT i) : SV_Target0
{
    float2 uv = i.texcoord0.xy;
    float4 rt0 = gbuffer0.Sample(previewSampler, uv);
    float4 rt1 = gbuffer1.Sample(previewSampler, uv);
    float4 rt2 = gbuffer2.Sample(previewSampler, uv);
    float4 rt3 = gbuffer3.Sample(previewSampler, uv);
    float depth = depthTexture.Sample(previewSampler, uv).r;
    int debugMode = (int)(previewDebugSettings.x + 0.5);

    // Raw MRT/depth inspector modes are intentionally unprocessed.
    if (debugMode == 1) return float4(rt0.rgb, 1.0); // RT0
    if (debugMode == 2) return float4(rt1.rgb, 1.0); // RT1
    if (debugMode == 3) return float4(rt2.rgb, 1.0); // RT2
    if (debugMode == 4) return float4(rt3.rgb, 1.0); // RT3
    if (debugMode == 5)
    {
        float d = saturate(depth);
        float v = 1.0 - pow(d, 0.22);
        return float4(v, v, v, 1.0);
    }
    if (debugMode == 12)
    {
        // Direct resource diagnostic: bypass mesh UVs and the GBuffer entirely.
        // The albedo SRV is sRGB-aware, so encode its linear sample back to the
        // UNORM desktop target for an apples-to-apples view of the source image.
        return float4(saturate(LinearToDisplay(previewMaterialAlbedoInput.Sample(previewSampler, uv).rgb)), 1.0);
    }

    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 viewRay = normalize(previewCameraForward.xyz +
        previewCameraRight.xyz * (ndc.x * previewCameraParams.x * previewCameraParams.y) +
        previewCameraUp.xyz * (ndc.y * previewCameraParams.y));

    if (depth >= 0.99999)
    {
        const int profile = (int)(previewDebugSettings.y + 0.5);
        if (profile == 2)
            return float4(previewBackgroundColor.rgb, 1.0);

        // Procedural lookdev floor. It is a viewport aid only and never changes
        // the BO3 shader/package being validated.
        if (previewLookdevSettings.z > 0.5 && viewRay.y < -0.0001)
        {
            float t = (-1.05 - previewCameraPosition.y) / viewRay.y;
            if (t > 0.0)
            {
                float3 p = previewCameraPosition.xyz + viewRay * t;
                if (max(abs(p.x), abs(p.z)) < 7.0)
                {
                    float2 cell = floor(p.xz * 2.0);
                    float checker = fmod(abs(cell.x + cell.y), 2.0);
                    float3 floorColor = lerp(float3(0.075,0.078,0.082), float3(0.105,0.108,0.114), checker);
                    float contact = exp(-dot(p.xz, p.xz) * 2.8) * previewLookdevSettings.w;
                    float ndotl = saturate(previewLightDirIntensity.y);
                    float3 floorEnv = (previewAmbientShadow.w > 0.5) ? EnvironmentDirection(float3(0.0, 1.0, 0.0)) : previewEnvironmentAmbient.rgb;
                    float3 floorLight = floorEnv * (previewAmbientShadow.x * 1.8) + previewLightColorFulbright.rgb * (ndotl * previewLightDirIntensity.w * 0.65);
                    floorColor *= max(float3(0.10, 0.10, 0.10), floorLight) * (1.0 - contact * 0.72);
                    return float4(ApplyLookdev(floorColor), 1.0);
                }
            }
        }
        if (previewAmbientShadow.w > 0.5)
            return float4(ApplyLookdev(EnvironmentAt(uv)), 1.0);
        return float4(ApplyLookdev(previewBackgroundColor.rgb), 1.0);
    }

    float3 albedo = max(rt0.rgb, 0.0);
    float3 emissive = max(rt3.rgb, 0.0); // preview-only fallback; BO3 opaque GBuffer has no emissive MRT
    const int materialProfile = (int)(previewDebugSettings.y + 0.5);

    // Semantic inspector views decode the actual BO3 GBuffer contract instead
    // of displaying packed channels as though they were ordinary RGB textures.
    if (debugMode == 6)
    {
        // RT0 stores linear albedo. APE Match/Neutral are presented through an
        // UNORM desktop swapchain, so encode for display when inspecting the
        // semantic albedo channel.
        const bool apeDisplay = materialProfile == 0 || materialProfile == 2;
        return float4(saturate(apeDisplay ? LinearToDisplay(albedo) : albedo), 1.0);
    }
    if (debugMode == 7)
    {
        float3 debugNormal = DecodeBo3GBufferNormal(rt1);
        return float4(debugNormal * 0.5 + 0.5, 1.0);
    }
    if (debugMode == 8)
    {
        float r = saturate(rt2.x);
        return float4(r, r, r, 1.0);
    }
    if (debugMode == 9)
    {
        float g = DecodeBo3Gloss(rt1.z);
        return float4(g, g, g, 1.0);
    }
    if (debugMode == 10)
    {
        float a = saturate(rt2.z);
        return float4(a, a, a, 1.0);
    }
    if (debugMode == 11) return float4(max(rt3.rgb, 0.0), 1.0);

    if (previewLightColorFulbright.w > 0.5 || materialProfile == 2)
        return float4(ApplyLookdev(albedo + emissive), 1.0);

    float3 N = DecodeBo3GBufferNormal(rt1);
    float3 L = normalize(previewLightDirIntensity.xyz);
    float3 V = normalize(-viewRay);
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N,L));
    float NdotV = saturate(dot(N,V));
    float NdotH = saturate(dot(N,H));
    float VdotH = saturate(dot(V,H));

    // BO3 GBuffer RT2 is ReflectanceOcclusion, not a conventional
    // (specular.rgb, gloss) texture. Generated procedural materials currently
    // use the stock dielectric 0.04 reflectance; RT2.z is occlusion. RT1.z owns
    // the packed gloss value.
    float reflectance = saturate(rt2.x);
    float3 specColor = max(float3(reflectance, reflectance, reflectance), float3(0.04, 0.04, 0.04));
    float gloss = DecodeBo3Gloss(rt1.z);
    float roughness = max(0.045, 1.0 - gloss);
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / max(3.14159265 * denom * denom, 1e-5);
    float k = (roughness + 1.0); k = (k*k) * 0.125;
    float Gv = NdotV / max(NdotV * (1.0-k) + k, 1e-5);
    float Gl = NdotL / max(NdotL * (1.0-k) + k, 1e-5);
    float3 F = specColor + (1.0 - specColor) * pow(1.0 - VdotH, 5.0);
    float3 specular = (D * Gv * Gl * F) / max(4.0 * NdotV * NdotL, 1e-4);

    float ao = saturate(max(rt2.z, 0.15));
    float3 ambientTint = previewEnvironmentAmbient.rgb;
    float3 ambient = albedo * ambientTint * (previewAmbientShadow.x * 1.35) * ao;
    if (previewAmbientShadow.w > 0.5 && previewAmbientShadow.z > 0.5)
    {
        float3 envDiffuse = EnvironmentDirection(N) * albedo * (previewAmbientShadow.x * 1.55) * ao;
        ambient = max(ambient, envDiffuse);
    }
    float3 diffuse = albedo * (1.0 - F) * (NdotL / 3.14159265);
    float shadowTerm = lerp(1.0, smoothstep(0.0, 0.35, NdotL), previewAmbientShadow.y);
    float3 direct = (diffuse + specular) * previewLightColorFulbright.rgb * previewLightDirIntensity.w * shadowTerm;

    float3 envSpec = 0.0;
    if (previewAmbientShadow.w > 0.5 && previewAmbientShadow.z > 0.5)
    {
        float3 R = reflect(-V, N);
        float3 env = EnvironmentDirection(R);
        envSpec = env * F * lerp(0.9, 0.18, roughness) * ao;
    }

    float3 color = ambient + direct + envSpec + emissive;
    return float4(ApplyLookdev(color), 1.0);
}
)";

        auto compileBlob = [&](const char* source, const char* debugName, const char* entry, const char* profile, ComPtr<ID3DBlob>& code) -> bool
        {
            ComPtr<ID3DBlob> errs;
            HRESULT hr = D3DCompile(source, strlen(source), debugName, nullptr, nullptr,
                                    entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                    code.GetAddressOf(), errs.GetAddressOf());
            if (FAILED(hr))
            {
                if (errs)
                    error = Utf8ToWide(std::string(static_cast<char*>(errs->GetBufferPointer()), errs->GetBufferSize()));
                else
                    error = L"Built-in preview shader failed to compile.";
                return false;
            }
            return true;
        };

        auto createVS = [&](const char* source, const char* debugName, ComPtr<ID3D11VertexShader>& out, ComPtr<ID3DBlob>* outBytecode = nullptr) -> bool
        {
            ComPtr<ID3DBlob> code;
            if (!compileBlob(source, debugName, "vs_main", "vs_5_0", code)) return false;
            HRESULT hr = device_->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out.GetAddressOf());
            if (FAILED(hr))
            {
                error = L"CreateVertexShader failed.";
                return false;
            }
            if (outBytecode) *outBytecode = code;
            return true;
        };

        auto createPS = [&](const char* source, const char* debugName, ComPtr<ID3D11PixelShader>& out) -> bool
        {
            ComPtr<ID3DBlob> code;
            if (!compileBlob(source, debugName, "ps_main", "ps_5_0", code)) return false;
            HRESULT hr = device_->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, out.GetAddressOf());
            if (FAILED(hr))
            {
                error = L"CreatePixelShader failed for a built-in preview helper.";
                return false;
            }
            return true;
        };

        if (!createVS(postFxSource, "preview_postfx_vs", postFxVertexShader_)) return false;
        if (!createVS(skySource, "preview_sky_vs", skyVertexShader_)) return false;
        ComPtr<ID3DBlob> materialVsCode;
        if (!createVS(materialSource, "preview_material_vs", materialVertexShader_, &materialVsCode)) return false;
        if (!createVS(directionalMaterialSource, "preview_directional_material_vs", directionalMaterialVertexShader_)) return false;
        if (!createVS(adaptedMaterialSource, "preview_adapted_material_vs", adaptedMaterialVertexShader_)) return false;
        if (!createVS(postFxSource, "preview_blit_vs", blitVertexShader_)) return false;
        if (!createPS(blitPsSource, "preview_blit_ps", blitPixelShader_)) return false;
        if (!createPS(runtimeSceneEncodePsSource, "preview_bo3_runtime_scene_encode_ps", runtimeSceneEncodePixelShader_)) return false;
        if (!createPS(runtimeSceneDecodePsSource, "preview_bo3_runtime_scene_decode_ps", runtimeSceneDecodePixelShader_)) return false;
        if (!createPS(environmentPsSource, "preview_environment_ps", environmentPixelShader_)) return false;

        D3D11_BUFFER_DESC runtimeSceneParamsDesc{};
        runtimeSceneParamsDesc.ByteWidth = 16;
        runtimeSceneParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
        runtimeSceneParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        runtimeSceneParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&runtimeSceneParamsDesc, nullptr, runtimeSceneParamsBuffer_.ReleaseAndGetAddressOf())))
        {
            error = L"CreateBuffer failed for BO3 runtime scene parameters.";
            return false;
        }
        if (!createPS(vertexPreviewPsSource, "preview_vertex_only_material_ps", vertexOnlyMaterialPixelShader_)) return false;
        if (!createPS(blitDepthPsSource, "preview_depth_blit_ps", blitDepthPixelShader_)) return false;
        if (!createPS(deferredLightPsSource, "preview_deferred_light_ps", deferredLightPixelShader_)) return false;

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MaterialVertex, position)), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(MaterialVertex, normal)),   D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(MaterialVertex, tangent)), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, static_cast<UINT>(offsetof(MaterialVertex, uv)),       D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        HRESULT hr = device_->CreateInputLayout(layout, ARRAYSIZE(layout), materialVsCode->GetBufferPointer(), materialVsCode->GetBufferSize(), materialInputLayout_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateInputLayout failed for the material preview vertex format.";
            return false;
        }
        return true;
    }

    bool CreateCameraBuffer(std::wstring& error)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 80; // four float4 values used by the built-in sky VS
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device_->CreateBuffer(&desc, nullptr, cameraVSBuffer_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateBuffer failed for the preview camera constant buffer.";
            return false;
        }
        return true;
    }

    bool DetectDeferredMaterialOutput(ID3DBlob* bytecode) const
    {
        if (!bytecode) return false;
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                              IID_PPV_ARGS(reflection.GetAddressOf()))))
            return false;

        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return false;
        bool hasTarget0 = false;
        bool hasHigherTarget = false;
        for (UINT i = 0; i < desc.OutputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflection->GetOutputParameterDesc(i, &param)) || !param.SemanticName)
                continue;
            if (_stricmp(param.SemanticName, "SV_Target") != 0 &&
                _stricmp(param.SemanticName, "SV_TARGET") != 0)
                continue;
            if (param.SemanticIndex == 0) hasTarget0 = true;
            else hasHigherTarget = true;
        }
        return hasTarget0 && hasHigherTarget;
    }

    bool DetectSkyShaderInput(ID3DBlob* bytecode) const
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
            return false;

        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return false;

        bool tex0HasDirection = false;
        bool tex1HasDirection = false;
        bool hasHigherTexcoord = false;
        for (UINT i = 0; i < desc.InputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflection->GetInputParameterDesc(i, &param)) || !param.SemanticName) continue;
            if (_stricmp(param.SemanticName, "TEXCOORD") != 0) continue;

            if (param.SemanticIndex == 0)
            {
                // xyz or xyzw TEXCOORD0 is the common BO3 sky-direction shape.
                tex0HasDirection = (param.Mask & 0x7) == 0x7;
            }
            else if (param.SemanticIndex == 1)
            {
                // Real BO3 sky shaders normally carry a second xyz direction
                // (fogDirection). A generic material's world-position TEXCOORD1
                // can also be xyz/xyzw, so reject richer material signatures below.
                tex1HasDirection = (param.Mask & 0x7) == 0x7;
            }
            else if (param.SemanticIndex >= 2)
            {
                // Normal/tangent/bitangent material inputs use TEXCOORD2+.
                // Treating these shaders as skies destroys their UV coordinates.
                hasHigherTexcoord = true;
            }
        }
        return tex0HasDirection && tex1HasDirection && !hasHigherTexcoord;
    }

    bool CreateSamplers(std::wstring& error)
    {
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        HRESULT hr = device_->CreateSamplerState(&desc, sampler_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateSamplerState failed.";
            return false;
        }

        D3D11_SAMPLER_DESC wrapDesc = desc;
        wrapDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        wrapDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        wrapDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        hr = device_->CreateSamplerState(&wrapDesc, materialWrapSampler_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Create material UV wrap sampler failed.";
            return false;
        }

        // Stock Geometry/lit's Color sampler defaults to `aniso2x (mip linear)`
        // with tiled U/V. Keep a dedicated s0 sampler so the parity test case
        // uses the same basic filtering without forcing normal/gloss/AO maps to
        // the color-map filter.
        D3D11_SAMPLER_DESC colorDesc = wrapDesc;
        colorDesc.Filter = D3D11_FILTER_ANISOTROPIC;
        colorDesc.MaxAnisotropy = 2;
        hr = device_->CreateSamplerState(&colorDesc, materialColorSampler_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"Create material color anisotropic sampler failed.";
            return false;
        }
        return true;
    }

    bool CreateMaterialRasterizer(std::wstring& error)
    {
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = D3D11_CULL_NONE;
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable = TRUE;
        HRESULT hr = device_->CreateRasterizerState(&desc, materialRasterizerState_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateRasterizerState failed for the 3D material preview.";
            return false;
        }
        desc.FillMode = D3D11_FILL_WIREFRAME;
        hr = device_->CreateRasterizerState(&desc, materialWireframeRasterizerState_.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateRasterizerState failed for the material wireframe preview.";
            return false;
        }
        return true;
    }

    bool CreateDefaultTextures(std::wstring& error)
    {
        constexpr UINT w = 1280;
        constexpr UINT h = 720;
        std::vector<uint8_t> source(static_cast<size_t>(w) * h * 4);
        std::vector<uint8_t> depth(static_cast<size_t>(w) * h * 4);
        for (UINT y = 0; y < h; ++y)
        {
            for (UINT x = 0; x < w; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                const float fx = static_cast<float>(x) / static_cast<float>(w - 1);
                const float fy = static_cast<float>(y) / static_cast<float>(h - 1);
                const bool grid = ((x / 64) % 2) ^ ((y / 64) % 2);
                source[i + 0] = static_cast<uint8_t>(45 + fx * 170.0f + (grid ? 12 : 0));
                source[i + 1] = static_cast<uint8_t>(40 + fy * 160.0f + (grid ? 8 : 0));
                source[i + 2] = static_cast<uint8_t>(70 + (1.0f - fx) * 120.0f);
                source[i + 3] = 255;
                const uint8_t d = static_cast<uint8_t>(std::clamp((fx * 0.65f + fy * 0.35f) * 255.0f, 0.0f, 255.0f));
                depth[i + 0] = d;
                depth[i + 1] = d;
                depth[i + 2] = d;
                depth[i + 3] = 255;
            }
        }
        if (!CreateTextureSRV(source.data(), w, h, sourceSRV_, error)) return false;
        if (!CreateTextureSRV(depth.data(), w, h, depthSRV_, error)) return false;
        const std::array<uint8_t, 4> neutralWhite{255, 255, 255, 255};
        // Flat Float-Z fallback for screenshots without a matching depth capture.
        // A constant depth is intentionally boring: it prevents the old generated
        // diagonal gradient from masquerading as real geometry in AO/outlines/fog.
        const std::array<uint8_t, 4> neutralDepth{96, 96, 96, 255};
        const std::array<uint8_t, 4> neutralNormal{128, 128, 255, 255};
        const std::array<uint8_t, 4> neutralBlack{0, 0, 0, 255};
        const std::array<uint8_t, 4> neutralSpecular{10, 10, 10, 255};
        // Stock Geometry/lit BASE_TEXTURES uses glossRange.y directly. APE's
        // material default is 13 on BO3's absolute 0..17 gloss scale, so the
        // optional Studio gloss slot must fall back to 13/17 rather than the
        // older arbitrary ~0.35 value.
        const uint8_t stockLitGloss = static_cast<uint8_t>(std::lround((13.0f / 17.0f) * 255.0f));
        const std::array<uint8_t, 4> neutralGloss{stockLitGloss, stockLitGloss, stockLitGloss, 255};
        if (!CreateTextureSRV(neutralWhite.data(), 1, 1, neutralSRV_, error)) return false;
        if (!CreateTextureSRV(neutralDepth.data(), 1, 1, neutralDepthSRV_, error)) return false;
        if (!CreateTextureSRV(neutralNormal.data(), 1, 1, neutralNormalSRV_, error)) return false;
        if (!CreateTextureSRV(neutralBlack.data(), 1, 1, neutralBlackSRV_, error)) return false;
        if (!CreateTextureSRV(neutralSpecular.data(), 1, 1, neutralSpecularSRV_, error)) return false;
        if (!CreateTextureSRV(neutralGloss.data(), 1, 1, neutralGlossSRV_, error)) return false;
        if (!CreateNeutral1DSRV(neutralWhite.data(), neutral1DSRV_, error)) return false;
        if (!CreateNeutral3DSRV(neutralWhite.data(), neutral3DSRV_, error)) return false;
        if (!CreateNeutralCubeSRV(neutralWhite.data(), neutralCubeSRV_, error)) return false;
        sourceWidth_ = depthWidth_ = w;
        sourceHeight_ = depthHeight_ = h;
        depthUserLoaded_ = false;
        builtInDepthScene_ = false;
        capturedBO3DepthScene_ = false;
        previewZNear_ = 0.1f;
        sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
        sourceHdrPeakLuminance_ = 1.0f;
        sourceHdrMeanLuminance_ = 0.18f;
        return true;
    }

    bool CreateMipmappedTextureSRV(const uint8_t* rgba, UINT w, UINT h,
                                   ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error,
                                   bool srgb = false)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 0;
        td.ArraySize = 1;
        td.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&td, nullptr, tex.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateTexture2D failed for Shadertoy channel.";
            return false;
        }

        context_->UpdateSubresource(tex.Get(), 0, nullptr, rgba, w * 4, 0);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = td.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MostDetailedMip = 0;
        sd.Texture2D.MipLevels = static_cast<UINT>(-1);
        hr = device_->CreateShaderResourceView(tex.Get(), &sd, out.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateShaderResourceView failed for Shadertoy channel.";
            return false;
        }

        context_->GenerateMips(out.Get());
        return true;
    }

    bool CreateFloatTextureSRV(const float* rgba, UINT w, UINT h,
                               ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error)
    {
        if (!rgba || w == 0 || h == 0)
        {
            error = L"Invalid floating-point texture data.";
            return false;
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = rgba;
        init.SysMemPitch = w * sizeof(float) * 4u;

        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&td, &init, tex.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateTexture2D failed for floating-point HDR source.";
            return false;
        }
        hr = device_->CreateShaderResourceView(tex.Get(), nullptr, out.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateShaderResourceView failed for floating-point HDR source.";
            return false;
        }
        return true;
    }

    bool CreateTextureSRV(const uint8_t* rgba, UINT w, UINT h,
                          ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = rgba;
        init.SysMemPitch = w * 4;
        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->CreateTexture2D(&td, &init, tex.GetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateTexture2D failed.";
            return false;
        }
        hr = device_->CreateShaderResourceView(tex.Get(), nullptr, out.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            error = L"CreateShaderResourceView failed.";
            return false;
        }
        return true;
    }

    bool CreateNeutral1DSRV(const uint8_t* rgba, ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error)
    {
        D3D11_TEXTURE1D_DESC td{};
        td.Width = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = rgba; init.SysMemPitch = 4;
        ComPtr<ID3D11Texture1D> tex;
        if (FAILED(device_->CreateTexture1D(&td, &init, tex.GetAddressOf()))) { error = L"Create neutral Texture1D failed."; return false; }
        if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, out.ReleaseAndGetAddressOf()))) { error = L"Create neutral Texture1D SRV failed."; return false; }
        return true;
    }

    bool CreateNeutral3DSRV(const uint8_t* rgba, ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error)
    {
        D3D11_TEXTURE3D_DESC td{};
        td.Width = 1; td.Height = 1; td.Depth = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = rgba; init.SysMemPitch = 4; init.SysMemSlicePitch = 4;
        ComPtr<ID3D11Texture3D> tex;
        if (FAILED(device_->CreateTexture3D(&td, &init, tex.GetAddressOf()))) { error = L"Create neutral Texture3D failed."; return false; }
        if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, out.ReleaseAndGetAddressOf()))) { error = L"Create neutral Texture3D SRV failed."; return false; }
        return true;
    }

    bool CreateNeutralCubeSRV(const uint8_t* rgba, ComPtr<ID3D11ShaderResourceView>& out, std::wstring& error)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 6; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        std::array<D3D11_SUBRESOURCE_DATA, 6> init{};
        for (auto& face : init) { face.pSysMem = rgba; face.SysMemPitch = 4; }
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device_->CreateTexture2D(&td, init.data(), tex.GetAddressOf()))) { error = L"Create neutral TextureCube failed."; return false; }
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = td.Format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; sd.TextureCube.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(tex.Get(), &sd, out.ReleaseAndGetAddressOf()))) { error = L"Create neutral TextureCube SRV failed."; return false; }
        return true;
    }

    static bool Is2DLike(D3D_SRV_DIMENSION dimension)
    {
        return dimension == D3D_SRV_DIMENSION_TEXTURE2D || dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY ||
               dimension == D3D_SRV_DIMENSION_TEXTURE2DMS || dimension == D3D_SRV_DIMENSION_TEXTURE2DMSARRAY;
    }

    ID3D11ShaderResourceView* NeutralForDimension(D3D_SRV_DIMENSION dimension) const
    {
        switch (dimension)
        {
        case D3D_SRV_DIMENSION_TEXTURE1D: return neutral1DSRV_.Get();
        case D3D_SRV_DIMENSION_TEXTURE3D: return neutral3DSRV_.Get();
        case D3D_SRV_DIMENSION_TEXTURECUBE: return neutralCubeSRV_.Get();
        case D3D_SRV_DIMENSION_TEXTURE2D: return neutralSRV_.Get();
        default: return nullptr; // arrays/MSAA/buffers need an exact matching resource type.
        }
    }

    bool ReflectBindableResourceNames(ID3DBlob* bytecode, QSet<QString>& out)
    {
        out.clear();
        ComPtr<ID3D11ShaderReflection> reflection;
        if (!bytecode || FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                                          IID_PPV_ARGS(reflection.GetAddressOf())))) return false;
        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return false;
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &bind)) || !bind.Name) continue;
            if (bind.Type != D3D_SIT_TEXTURE && bind.Type != D3D_SIT_SAMPLER) continue;
            out.insert(QString::fromUtf8(bind.Name));
        }
        return true;
    }

    void ReflectTextureResources(ID3DBlob* bytecode, std::vector<ReflectedResource>& out)
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())))) return;
        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return;
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D11_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDesc(i, &bind)) || !bind.Name) continue;
            if (bind.Type != D3D_SIT_TEXTURE) continue;
            ReflectedResource resource{};
            resource.name = bind.Name;
            resource.slot = bind.BindPoint;
            resource.bindCount = std::max(1u, bind.BindCount);
            resource.dimension = bind.Dimension;
            out.push_back(std::move(resource));
        }
    }

    bool CreateUserMaterialInputLayout(ID3DBlob* bytecode, ComPtr<ID3D11InputLayout>& outLayout, std::wstring& error)
    {
        outLayout.Reset();
        if (!bytecode) return false;

        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf()))))
        {
            error += L"\r\nCould not reflect the vertex shader input signature.";
            return false;
        }
        D3D11_SHADER_DESC shaderDesc{};
        if (FAILED(reflection->GetDesc(&shaderDesc))) return false;

        std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
        layout.reserve(shaderDesc.InputParameters);
        for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC param{};
            if (FAILED(reflection->GetInputParameterDesc(i, &param)) || !param.SemanticName) continue;
            if (param.SystemValueType != D3D_NAME_UNDEFINED) continue;

            D3D11_INPUT_ELEMENT_DESC d{};
            d.SemanticName = param.SemanticName;
            d.SemanticIndex = param.SemanticIndex;
            d.InputSlot = 0;
            d.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            d.InstanceDataStepRate = 0;

            if (_stricmp(param.SemanticName, "POSITION") == 0 && param.SemanticIndex == 0)
            {
                d.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                d.AlignedByteOffset = static_cast<UINT>(offsetof(MaterialVertex, position));
            }
            else if (_stricmp(param.SemanticName, "NORMAL") == 0 && param.SemanticIndex == 0)
            {
                d.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                d.AlignedByteOffset = static_cast<UINT>(offsetof(MaterialVertex, normal));
            }
            else if (_stricmp(param.SemanticName, "TANGENT") == 0 && param.SemanticIndex == 0)
            {
                d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                d.AlignedByteOffset = static_cast<UINT>(offsetof(MaterialVertex, tangent));
            }
            else if (_stricmp(param.SemanticName, "TEXCOORD") == 0 && param.SemanticIndex == 0)
            {
                d.Format = DXGI_FORMAT_R32G32_FLOAT;
                d.AlignedByteOffset = static_cast<UINT>(offsetof(MaterialVertex, uv));
            }
            else
            {
                error += L"\r\n[Vertex preview] Unsupported mesh input semantic: ";
                error += Utf8ToWide(param.SemanticName);
                error += std::to_wstring(param.SemanticIndex);
                error += L". Supported inputs are POSITION0, NORMAL0, TANGENT0, and TEXCOORD0.";
                return false;
            }
            layout.push_back(d);
        }

        if (layout.empty())
            return true; // SV_VertexID-only vertex shader.

        const HRESULT hr = device_->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()),
            bytecode->GetBufferPointer(), bytecode->GetBufferSize(), outLayout.GetAddressOf());
        if (FAILED(hr))
        {
            error += L"\r\nCreateInputLayout failed for the vertex-displacement shader (0x" + Hex(hr) + L").";
            return false;
        }
        return true;
    }

    void ReflectShaderPerformance(ID3DBlob* bytecode, ShaderPerformanceStats& out)
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())))) return;
        D3D11_SHADER_DESC desc{};
        if (FAILED(reflection->GetDesc(&desc))) return;
        out.instructionCount = desc.InstructionCount;
        out.tempRegisterCount = desc.TempRegisterCount;
        out.textureNormalInstructions = desc.TextureNormalInstructions;
        out.textureLoadInstructions = desc.TextureLoadInstructions;
        out.textureCompInstructions = desc.TextureCompInstructions;
        out.textureBiasInstructions = desc.TextureBiasInstructions;
        out.textureGradientInstructions = desc.TextureGradientInstructions;
        out.dynamicFlowControlCount = desc.DynamicFlowControlCount;
    }

    void ReflectConstantBuffers(ID3DBlob* bytecode, std::vector<ReflectedCBuffer>& out)
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        if (FAILED(D3DReflect(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), IID_PPV_ARGS(reflection.GetAddressOf())))) return;
        D3D11_SHADER_DESC shaderDesc{};
        if (FAILED(reflection->GetDesc(&shaderDesc))) return;

        for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
        {
            ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);
            if (!cb) continue;
            D3D11_SHADER_BUFFER_DESC cbDesc{};
            if (FAILED(cb->GetDesc(&cbDesc)) || !cbDesc.Name || cbDesc.Size == 0) continue;

            D3D11_SHADER_INPUT_BIND_DESC bind{};
            if (FAILED(reflection->GetResourceBindingDescByName(cbDesc.Name, &bind))) continue;
            if (bind.Type != D3D_SIT_CBUFFER) continue;

            ReflectedCBuffer info{};
            info.slot = bind.BindPoint;
            info.byteSize = (cbDesc.Size + 15u) & ~15u;

            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = info.byteSize;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device_->CreateBuffer(&bd, nullptr, info.buffer.GetAddressOf()))) continue;

            auto appendToolsgfxSceneMembers = [&](auto&& self, ID3D11ShaderReflectionType* type,
                                                  UINT baseOffset, const std::string& path) -> void
            {
                if (!type) return;
                D3D11_SHADER_TYPE_DESC td{};
                if (FAILED(type->GetDesc(&td))) return;
                if (td.Class == D3D_SVC_STRUCT && td.Members > 0)
                {
                    for (UINT member = 0; member < td.Members; ++member)
                    {
                        auto* memberType = type->GetMemberTypeByIndex(member);
                        const char* memberName = type->GetMemberTypeName(member);
                        if (!memberType || !memberName) continue;
                        D3D11_SHADER_TYPE_DESC md{};
                        if (FAILED(memberType->GetDesc(&md))) continue;
                        self(self, memberType, baseOffset + md.Offset,
                             path.empty() ? std::string(memberName) : path + "." + memberName);
                    }
                    return;
                }

                // Only flatten the CodeScene fields required by the explicit
                // TOOLSGFX postfx preview path. Keeping the whitelist narrow
                // avoids turning the very large gScene struct into hundreds of
                // accidental user-editable parameters.
                static const std::unordered_set<std::string> wanted = {
                    "gScene.renderTargetSize", "gScene.renderTargetInvSize", "gScene.time",
                    "gScene.exposure", "gScene.invExposure", "gScene.exposureClamped",
                    "gScene.skyRotation", "gScene.skySize", "gScene.skyTransition",
                    "gScene.wldCameraPosition"
                };
                if (wanted.find(path) == wanted.end()) return;

                const size_t dot = path.find_last_of('.');
                ReflectedVariable rv{};
                rv.name = dot == std::string::npos ? path : path.substr(dot + 1);
                rv.offset = baseOffset;
                rv.size = std::max<UINT>(4u, td.Rows * td.Columns * 4u);
                rv.varClass = td.Class;
                rv.varType = td.Type;
                rv.rows = td.Rows;
                rv.columns = td.Columns;
                rv.elements = td.Elements;
                info.variables.push_back(std::move(rv));
            };

            for (UINT v = 0; v < cbDesc.Variables; ++v)
            {
                auto* var = cb->GetVariableByIndex(v);
                if (!var) continue;
                D3D11_SHADER_VARIABLE_DESC vd{};
                if (FAILED(var->GetDesc(&vd)) || !vd.Name) continue;
                ReflectedVariable rv{};
                rv.name = vd.Name;
                rv.offset = vd.StartOffset;
                rv.size = vd.Size;
                if (auto* type = var->GetType())
                {
                    D3D11_SHADER_TYPE_DESC td{};
                    if (SUCCEEDED(type->GetDesc(&td)))
                    {
                        if (_stricmp(vd.Name, "gScene") == 0 && td.Class == D3D_SVC_STRUCT)
                        {
                            appendToolsgfxSceneMembers(appendToolsgfxSceneMembers, type, vd.StartOffset, "gScene");
                            continue;
                        }
                        rv.varClass = td.Class;
                        rv.varType = td.Type;
                        rv.rows = td.Rows;
                        rv.columns = td.Columns;
                        rv.elements = td.Elements;
                    }
                }
                info.variables.push_back(std::move(rv));
            }
            out.push_back(std::move(info));
        }
    }

    static bool SameName(const std::string& a, const char* b)
    {
        return _stricmp(a.c_str(), b) == 0;
    }

    bool HasReflectedVariable(const char* name) const
    {
        for (const auto& cb : cbuffers_)
            for (const auto& v : cb.variables)
                if (SameName(v.name, name)) return true;
        return false;
    }

    void ApplyPreviewParameterDefaults()
    {
        auto setIfMissing = [this](const char* name, float value)
        {
            if (HasReflectedVariable(name) && customShaderParameters_.find(name) == customShaderParameters_.end())
                customShaderParameters_[name] = value;
        };

        // Adapter-generated BO3 Material constants live in the default material
        // cbuffer. A source initializer such as `float bo3MaterialOutputScale = 1`
        // is NOT a runtime cbuffer value: D3D11 receives zero until the host writes
        // it. That made valid converted materials compile successfully but render
        // completely black because their RGB was multiplied by zero. Mirror the
        // export/GDT defaults here so the strict preview and BO3 package start from
        // the same material values. Explicit user preview overrides still win.
        setIfMissing("bo3MaterialOutputScale", 1.0f);
        setIfMissing("bo3MaterialOpacity", 1.0f);
        setIfMissing("bo3MaterialAlphaCutoff", 0.5f);

        // BO3's stock fisheye shader receives these values from the material/tool
        // runtime. HLSL reflection cannot recover those asset values, so give the
        // preview a useful visible starting point instead of the all-zero cbuffer.
        if (HasReflectedVariable("radius") && HasReflectedVariable("density") && HasReflectedVariable("intensity"))
        {
            setIfMissing("radius", 0.50f);
            setIfMissing("density", 1.00f);
            setIfMissing("intensity", 0.35f);
            setIfMissing("invertGradient", 0.0f);
            setIfMissing("invertDensity", 0.0f);
            setIfMissing("displayGradient", 0.0f);
        }
    }

    struct CameraFrameData
    {
        std::array<float, 4> right{};
        std::array<float, 4> up{};
        std::array<float, 4> forward{};
        std::array<float, 4> params{};
        std::array<float, 16> view{};
        std::array<float, 16> projection{};
        std::array<float, 16> viewProjection{};
        std::array<float, 16> inverseView{};
        std::array<float, 16> inverseProjection{};
        std::array<float, 16> inverseViewProjection{};
    };

    CameraFrameData BuildCameraFrameForState(
        const D3D11_VIEWPORT& vp, float yawDegrees, float pitchDegrees,
        float fovDegrees, const std::array<float, 3>& position) const
    {
        CameraFrameData out{};
        const float yaw = DirectX::XMConvertToRadians(yawDegrees);
        const float pitch = DirectX::XMConvertToRadians(pitchDegrees);
        const float cp = std::cos(pitch);
        const float sp = std::sin(pitch);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);

        const DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMVectorSet(cp * cy, cp * sy, sp, 0.0f));
        const DirectX::XMVECTOR worldUp = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        DirectX::XMVECTOR right = DirectX::XMVector3Cross(worldUp, forward);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(right)) < 0.000001f)
            right = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        right = DirectX::XMVector3Normalize(right);
        const DirectX::XMVECTOR up = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(forward, right));

        DirectX::XMFLOAT3 rf{}, uf{}, ff{};
        DirectX::XMStoreFloat3(&rf, right);
        DirectX::XMStoreFloat3(&uf, up);
        DirectX::XMStoreFloat3(&ff, forward);
        out.right = {rf.x, rf.y, rf.z, 0.0f};
        out.up = {uf.x, uf.y, uf.z, 0.0f};
        out.forward = {ff.x, ff.y, ff.z, 0.0f};

        const float aspect = std::max(0.01f, vp.Width / std::max(1.0f, vp.Height));
        const float fovRadians = DirectX::XMConvertToRadians(fovDegrees);
        out.params = {aspect, std::tan(fovRadians * 0.5f), yawDegrees, pitchDegrees};

        const DirectX::XMVECTOR eye = DirectX::XMVectorSet(position[0], position[1], position[2], 1.0f);
        const DirectX::XMMATRIX view = DirectX::XMMatrixLookToLH(eye, forward, up);
        const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(fovRadians, aspect, 0.1f, 10000.0f);
        const DirectX::XMMATRIX viewProjection = DirectX::XMMatrixMultiply(view, projection);
        DirectX::XMVECTOR determinant = DirectX::XMVectorZero();
        const DirectX::XMMATRIX inverseView = DirectX::XMMatrixInverse(&determinant, view);
        const DirectX::XMMATRIX inverseProjection = DirectX::XMMatrixInverse(&determinant, projection);
        const DirectX::XMMATRIX inverseViewProjection = DirectX::XMMatrixInverse(&determinant, viewProjection);

        auto storeMatrix = [](const DirectX::XMMATRIX& matrix, std::array<float, 16>& target)
        {
            DirectX::XMFLOAT4X4 value{};
            DirectX::XMStoreFloat4x4(&value, matrix);
            std::memcpy(target.data(), &value, sizeof(value));
        };
        storeMatrix(view, out.view);
        storeMatrix(projection, out.projection);
        storeMatrix(viewProjection, out.viewProjection);
        storeMatrix(inverseView, out.inverseView);
        storeMatrix(inverseProjection, out.inverseProjection);
        storeMatrix(inverseViewProjection, out.inverseViewProjection);
        return out;
    }

    CameraFrameData BuildCameraFrame(const D3D11_VIEWPORT& vp) const
    {
        return BuildCameraFrameForState(vp, cameraYawDegrees_, cameraPitchDegrees_,
                                        cameraFovDegrees_, cameraPosition_);
    }

    CameraFrameData BuildShaderInputCameraFrame(const D3D11_VIEWPORT& vp) const
    {
        if(!shaderDrivenMovementDisabled_) return BuildCameraFrame(vp);
        return BuildCameraFrameForState(vp, shaderMovementFrozenYaw_,
            shaderMovementFrozenPitch_, shaderMovementFrozenFov_,
            shaderMovementFrozenPosition_);
    }

    void UpdateCameraVertexBuffer(const D3D11_VIEWPORT& vp, bool honorShaderMovementLock = true)
    {
        if (!cameraVSBuffer_) return;
        const CameraFrameData camera = honorShaderMovementLock
            ? BuildShaderInputCameraFrame(vp) : BuildCameraFrame(vp);
        std::array<float, 16> bytes{};
        std::copy(camera.right.begin(), camera.right.end(), bytes.begin());
        std::copy(camera.up.begin(), camera.up.end(), bytes.begin() + 4);
        std::copy(camera.forward.begin(), camera.forward.end(), bytes.begin() + 8);
        std::copy(camera.params.begin(), camera.params.end(), bytes.begin() + 12);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(cameraVSBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, bytes.data(), sizeof(bytes));
            context_->Unmap(cameraVSBuffer_.Get(), 0);
        }
        ID3D11Buffer* buffer = cameraVSBuffer_.Get();
        context_->VSSetConstantBuffers(0, 1, &buffer);
    }

    void UpdateConstantBuffers(const D3D11_VIEWPORT& vp)
    {
        POINT mouse{};
        GetCursorPos(&mouse);
        ScreenToClient(hwnd_, &mouse);
        const float mx = shaderDrivenMovementDisabled_ ? 0.0f : static_cast<float>(mouse.x) - vp.TopLeftX;
        const float my = shaderDrivenMovementDisabled_ ? 0.0f : static_cast<float>(mouse.y) - vp.TopLeftY;
        const float left = shaderDrivenMovementDisabled_ ? 0.0f :
            ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1.0f : 0.0f);
        const float right = shaderDrivenMovementDisabled_ ? 0.0f :
            ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1.0f : 0.0f);

        // BO3's renderTargetSize describes the actual render texture, not the
        // size of the preview pane on screen. Pixel-sized effects must therefore
        // use the loaded t0 dimensions even when the preview is scaled down.
        const float targetW = sourceWidth_ > 0 ? static_cast<float>(sourceWidth_) : vp.Width;
        const float targetH = sourceHeight_ > 0 ? static_cast<float>(sourceHeight_) : vp.Height;
        const std::array<float, 4> renderSize{targetW, targetH, 1.0f / targetW, 1.0f / targetH};
        const std::array<float, 4> invSize{1.0f / targetW, 1.0f / targetH, targetW, targetH};
        const std::array<uint32_t, 2> renderSizeUint{
            static_cast<uint32_t>(std::max(1.0f, targetW)),
            static_cast<uint32_t>(std::max(1.0f, targetH))};
        const std::array<float, 2> renderInvSize2{1.0f / targetW, 1.0f / targetH};
        const std::array<float, 2> toolsgfxSkyRotation{0.0f, 0.0f};
        const float shaderTime = shaderDrivenMovementDisabled_ ? shaderMovementFrozenTime_ : elapsed_;
        const uint64_t shaderFrame = shaderDrivenMovementDisabled_ ? shaderMovementFrozenFrame_ : frameNumber_;
        const float dt = shaderDrivenMovementDisabled_ ? 0.0f : lastDelta_;
        const std::array<float, 4> gameTime{shaderTime, dt, static_cast<float>(shaderFrame), shaderTime};
        const std::array<float, 4> mouseData{mx, my, left, right};
        const CameraFrameData camera = BuildShaderInputCameraFrame(vp);

        // Material/orbit-camera matrices used when a vertex-only displacement
        // shader is being previewed. These mirror the built-in material VS so a
        // standalone vs_main can use common matrix names without needing a ps_main.
        std::array<float, 16> materialWorld{};
        std::array<float, 16> materialView{};
        std::array<float, 16> materialProjection{};
        std::array<float, 16> materialWorldViewProj{};
        std::array<float, 4> materialCameraPosition{0,0,0,1};
        if (vertexOnlyShader_)
        {
            using namespace DirectX;
            const float aspect = std::max(0.001f, vp.Width / std::max(1.0f, vp.Height));
            const float yaw = XMConvertToRadians(cameraYawDegrees_);
            const float pitch = XMConvertToRadians(cameraPitchDegrees_);
            const float radius = (previewMesh_ == PreviewMesh::Plane || previewMesh_ == PreviewMesh::Card) ? std::max(3.0f, cameraDistance_) : cameraDistance_;
            XMVECTOR target = XMVectorZero();
            XMVECTOR eye = XMVectorSet(0.0f, 0.0f, -radius, 1.0f);
            const XMMATRIX rotation = XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
            eye = XMVector3TransformCoord(eye, rotation);
            XMVECTOR panOffset = XMVectorSet(cameraPanX_, cameraPanY_, 0.0f, 0.0f);
            panOffset = XMVector3TransformNormal(panOffset, rotation);
            target = XMVectorAdd(target, panOffset);
            eye = XMVectorAdd(eye, target);
            const XMMATRIX mView = XMMatrixLookAtLH(eye, target, XMVectorSet(0,1,0,0));
            const XMMATRIX mProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), aspect, 0.05f, 100.0f);
            XMMATRIX mWorld = XMMatrixIdentity();
            if (previewMesh_ == PreviewMesh::Plane || previewMesh_ == PreviewMesh::Card) mWorld = XMMatrixScaling(1.8f, 1.8f, 1.8f);
            const XMMATRIX mWvp = mWorld * mView * mProj;
            auto storeTransposed = [](const XMMATRIX& m, std::array<float,16>& dst)
            {
                XMFLOAT4X4 temp{};
                XMStoreFloat4x4(&temp, XMMatrixTranspose(m));
                std::memcpy(dst.data(), &temp, sizeof(temp));
            };
            storeTransposed(mWorld, materialWorld);
            storeTransposed(mView, materialView);
            storeTransposed(mProj, materialProjection);
            storeTransposed(mWvp, materialWorldViewProj);
            XMFLOAT3 eye3{};
            XMStoreFloat3(&eye3, eye);
            materialCameraPosition = {eye3.x, eye3.y, eye3.z, 1.0f};
        }

        const float previewModeFlag =
            previewMode_ == PreviewMode::DeferredGBuffer ? 3.0f :
            (previewMode_ == PreviewMode::ForwardMaterial ? 2.0f :
            (previewMode_ == PreviewMode::Sky ? 1.0f : 0.0f));
        const float shaderYaw = shaderDrivenMovementDisabled_ ? shaderMovementFrozenYaw_ : cameraYawDegrees_;
        const float shaderPitch = shaderDrivenMovementDisabled_ ? shaderMovementFrozenPitch_ : cameraPitchDegrees_;
        const float shaderFov = shaderDrivenMovementDisabled_ ? shaderMovementFrozenFov_ : cameraFovDegrees_;
        const std::array<float, 4> params{shaderYaw, shaderPitch, shaderFov, previewModeFlag};
        const std::array<float, 4> previewZNear{std::max(previewZNear_, 0.000001f), 0.0f, 0.0f, 0.0f};
        const std::array<float, 4> one4{1.0f, 1.0f, 1.0f, 1.0f};
        const float runtimeExposureScale = std::exp2(postFxRuntimeExposureEV_);
        const float runtimeInvExposure = runtimeExposureScale > 0.000001f ? 1.0f / runtimeExposureScale : 1.0f;
        const std::array<float, 4> relativeHdrExposure{runtimeExposureScale, runtimeInvExposure, runtimeExposureScale, 0.0f};
        const std::array<float, 4> colorMatrixR{1.0f, 0.0f, 0.0f, 0.0f};
        const std::array<float, 4> colorMatrixG{0.0f, 1.0f, 0.0f, 0.0f};
        const std::array<float, 4> colorMatrixB{0.0f, 0.0f, 1.0f, 0.0f};
        const auto& shaderCameraPosition = shaderDrivenMovementDisabled_
            ? shaderMovementFrozenPosition_ : cameraPosition_;
        const std::array<float, 4> cameraPosition{shaderCameraPosition[0], shaderCameraPosition[1], shaderCameraPosition[2], 1.0f};
        const float ly = DirectX::XMConvertToRadians(lightYawDegrees_);
        const float lp = DirectX::XMConvertToRadians(lightPitchDegrees_);
        const std::array<float, 4> previewLightDirection{
            std::cos(lp) * std::cos(ly), std::sin(lp), std::cos(lp) * std::sin(ly), lightIntensity_};
        const std::array<float, 4> previewLightSettings{lightIntensity_, ambientIntensity_, shadowStrength_, fulbright_ ? 1.0f : 0.0f};
        const auto effectiveEnvironment = (environmentAffectsLighting_ && environmentEnabled_) ? environmentAverageColor_ : std::array<float,3>{0.26f, 0.26f, 0.28f};
        const auto effectiveSunColor = (environmentAffectsLighting_ && environmentEnabled_) ? environmentSunColor_ : std::array<float,3>{1.0f, 1.0f, 1.0f};
        const std::array<float,4> previewEnvironmentColor{effectiveEnvironment[0], effectiveEnvironment[1], effectiveEnvironment[2], environmentEnabled_ ? 1.0f : 0.0f};
        const std::array<float,4> previewSunColor{effectiveSunColor[0], effectiveSunColor[1], effectiveSunColor[2], 1.0f};

        for (auto& cb : cbuffers_)
        {
            std::vector<uint8_t> bytes(cb.byteSize, 0);
            for (const auto& v : cb.variables)
            {
                const void* src = nullptr;
                UINT srcBytes = 0;
                float scalar = 0.0f;
                int32_t intScalar = 0;
                uint32_t uintScalar = 0;
                std::array<float, 4> scriptValue{0, 0, 0, 0};

                if (SameName(v.name, "renderTargetSize") || SameName(v.name, "targetSize") ||
                    SameName(v.name, "screenSize") || SameName(v.name, "viewSize") || SameName(v.name, "resolution"))
                {
                    if (v.varType == D3D_SVT_UINT && v.columns <= 2)
                    {
                        src = renderSizeUint.data(); srcBytes = 8;
                    }
                    else
                    {
                        src = renderSize.data(); srcBytes = 16;
                    }
                }
                else if (SameName(v.name, "renderTargetInvSize"))
                {
                    src = renderInvSize2.data(); srcBytes = 8;
                }
                else if (SameName(v.name, "invRenderTargetSize") || SameName(v.name, "invTargetSize"))
                {
                    src = invSize.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "gameTime"))
                {
                    src = gameTime.data(); srcBytes = 16;
                }
                else if (v.name.size() == 13 &&
                         _strnicmp(v.name.c_str(), "scriptVector", 12) == 0 &&
                         v.name[12] >= '0' && v.name[12] <= '7')
                {
                    const int vectorIndex = v.name[12] - '0';
                    scriptValue = GetEffectiveScriptVector(vectorIndex);
                    src = scriptValue.data(); srcBytes = 16;
                }
                else if (auto customIt = customShaderParameters_.find(v.name); customIt != customShaderParameters_.end())
                {
                    const float value = customIt->second;
                    if (v.varType == D3D_SVT_BOOL || v.varType == D3D_SVT_UINT)
                    {
                        uintScalar = (v.varType == D3D_SVT_BOOL) ? (value != 0.0f ? 1u : 0u) : static_cast<uint32_t>(std::max(0.0f, std::round(value)));
                        src = &uintScalar; srcBytes = 4;
                    }
                    else if (v.varType == D3D_SVT_INT)
                    {
                        intScalar = static_cast<int32_t>(std::lround(value));
                        src = &intScalar; srcBytes = 4;
                    }
                    else
                    {
                        scalar = value; src = &scalar; srcBytes = 4;
                    }
                }
                else if (vertexOnlyShader_ &&
                         (SameName(v.name, "worldViewProj") || SameName(v.name, "worldViewProjection") ||
                          SameName(v.name, "worldViewProjectionMatrix") || SameName(v.name, "modelViewProjection") ||
                          SameName(v.name, "modelViewProjectionMatrix")))
                {
                    src = materialWorldViewProj.data(); srcBytes = 64;
                }
                else if (vertexOnlyShader_ &&
                         (SameName(v.name, "world") || SameName(v.name, "worldMatrix") ||
                          SameName(v.name, "model") || SameName(v.name, "modelMatrix")))
                {
                    src = materialWorld.data(); srcBytes = 64;
                }
                else if (vertexOnlyShader_ &&
                         (SameName(v.name, "view") || SameName(v.name, "materialView") || SameName(v.name, "materialViewMatrix")))
                {
                    src = materialView.data(); srcBytes = 64;
                }
                else if (vertexOnlyShader_ &&
                         (SameName(v.name, "projection") || SameName(v.name, "materialProjection") || SameName(v.name, "materialProjectionMatrix")))
                {
                    src = materialProjection.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "projectionMatrix"))
                {
                    src = vertexOnlyShader_ ? materialProjection.data() : camera.projection.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "viewMatrix"))
                {
                    src = vertexOnlyShader_ ? materialView.data() : camera.view.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "viewProjectionMatrix"))
                {
                    src = vertexOnlyShader_ ? materialWorldViewProj.data() : camera.viewProjection.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "inverseProjectionMatrix"))
                {
                    src = camera.inverseProjection.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "inverseViewMatrix"))
                {
                    src = camera.inverseView.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "inverseViewProjectionMatrix"))
                {
                    src = camera.inverseViewProjection.data(); srcBytes = 64;
                }
                else if (SameName(v.name, "cameraLook"))
                {
                    src = camera.forward.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "cameraSide"))
                {
                    src = camera.right.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "cameraUp"))
                {
                    src = camera.up.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "eyeOffset") || SameName(v.name, "cameraPosition") || SameName(v.name, "previewCameraPos"))
                {
                    src = vertexOnlyShader_ ? materialCameraPosition.data() : cameraPosition.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "previewLightDir") || SameName(v.name, "previewLightDirection") ||
                         SameName(v.name, "lightDirection") || SameName(v.name, "lightDir") || SameName(v.name, "sunDirection"))
                {
                    src = previewLightDirection.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "previewLightSettings"))
                {
                    src = previewLightSettings.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "lightIntensity") || SameName(v.name, "previewLightIntensity"))
                {
                    scalar = lightIntensity_; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "ambientIntensity") || SameName(v.name, "previewAmbient"))
                {
                    scalar = ambientIntensity_; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "shadowStrength") || SameName(v.name, "previewShadowStrength"))
                {
                    scalar = shadowStrength_; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "environmentColor") || SameName(v.name, "previewEnvironmentColor") ||
                         SameName(v.name, "skyColor") || SameName(v.name, "ambientColor"))
                {
                    src = previewEnvironmentColor.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "sunColor") || SameName(v.name, "lightColor") || SameName(v.name, "previewSunColor"))
                {
                    src = previewSunColor.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "fulbright") || SameName(v.name, "previewFulbright"))
                {
                    scalar = fulbright_ ? 1.0f : 0.0f; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "downSamples"))
                {
                    scalar = downSamplesValue_; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "exposure"))
                {
                    scalar = runtimeExposureScale; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "invExposure"))
                {
                    scalar = runtimeInvExposure; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "exposureClamped"))
                {
                    scalar = runtimeExposureScale; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "wldCameraPosition"))
                {
                    src = cameraPosition.data(); srcBytes = 12;
                }
                else if (SameName(v.name, "skyRotation"))
                {
                    src = toolsgfxSkyRotation.data(); srcBytes = 8;
                }
                else if (SameName(v.name, "skySize") || SameName(v.name, "skyTransition"))
                {
                    scalar = SameName(v.name, "skySize") ? 1.0f : 0.0f; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "materialColor"))
                {
                    src = one4.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "relHDRExposure"))
                {
                    src = relativeHdrExposure.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "colorMatrixR"))
                {
                    src = colorMatrixR.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "colorMatrixG"))
                {
                    src = colorMatrixG.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "colorMatrixB"))
                {
                    src = colorMatrixB.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "upscaledTargetSize") || SameName(v.name, "viewportDimensions"))
                {
                    src = renderSize.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "zNear"))
                {
                    src = previewZNear.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "previewMouse") || SameName(v.name, "mouse"))
                {
                    src = mouseData.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "previewParams"))
                {
                    src = params.data(); srcBytes = 16;
                }
                else if (SameName(v.name, "time") || SameName(v.name, "elapsedTime"))
                {
                    scalar = shaderTime; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "deltaTime"))
                {
                    scalar = dt; src = &scalar; srcBytes = 4;
                }
                else if (SameName(v.name, "frameCount") || SameName(v.name, "frameNumber"))
                {
                    scalar = static_cast<float>(shaderFrame); src = &scalar; srcBytes = 4;
                }

                if (src && v.offset < bytes.size())
                {
                    const UINT copyBytes = std::min({srcBytes, v.size, static_cast<UINT>(bytes.size() - v.offset)});
                    memcpy(bytes.data() + v.offset, src, copyBytes);
                }
            }

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context_->Map(cb.buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                memcpy(mapped.pData, bytes.data(), bytes.size());
                context_->Unmap(cb.buffer.Get(), 0);
            }
            ID3D11Buffer* b = cb.buffer.Get();
            if (vertexOnlyShader_)
                context_->VSSetConstantBuffers(cb.slot, 1, &b);
            else
                context_->PSSetConstantBuffers(cb.slot, 1, &b);
        }
    }

    HWND hwnd_ = nullptr;
    LONG width_ = 1;
    LONG height_ = 1;
    UINT sourceWidth_ = 0, sourceHeight_ = 0;
    UINT depthWidth_ = 0, depthHeight_ = 0;
    PreviewSourceEncoding sourceEncoding_ = PreviewSourceEncoding::LdrSrgb;
    float sourceHdrPeakLuminance_ = 1.0f;
    float sourceHdrMeanLuminance_ = 0.18f;
    float postFxRuntimeExposureEV_ = bo3::kDefaultBo3RuntimeSceneExposureEv;
    bool paused_ = false;
    bool shaderDrivenMovementDisabled_ = false;
    float shaderMovementFrozenTime_ = 0.0f;
    uint64_t shaderMovementFrozenFrame_ = 0;
    float shaderMovementFrozenYaw_ = 0.0f;
    float shaderMovementFrozenPitch_ = 0.0f;
    float shaderMovementFrozenFov_ = 64.0f;
    std::array<float, 3> shaderMovementFrozenPosition_{0.0f, 0.0f, 0.0f};
    bool skyShaderMode_ = false;
    bool vertexOnlyShader_ = false;
    bool adaptedMaterialShader_ = false;
    bool deferredMaterialShader_ = false;
    bool packagePreviewSuppressed_ = false;
    bool liveCaptureSource_ = false;
    LiveComparisonMode liveComparisonMode_ = LiveComparisonMode::ProcessedOnly;
    float liveSplitFraction_ = 0.5f;
    QString liveCaptureNotification_;
    bo3::LiveWindowCapture liveWindowCapture_;
    bool downsamplePassDetected_ = false;
    bool upsamplingPassDetected_ = false;
    float downSamplesValue_ = 0.0f;
    bool bo3NeutralDefaults_ = true;
    std::unordered_map<std::string, bo3::PackageResourceRole> previewResourceRoles_{};
    QSet<QString> optimizedPixelResourceNames_{};
    bool optimizedPixelResourceReflectionValid_ = false;
    std::unordered_map<std::string, float> customShaderParameters_{};
    std::array<bool, 8> detectedScriptVectors_{};
    std::array<bool, 8> scriptVectorCustom_{};
    std::array<std::array<float, 4>, 8> scriptVectorValues_{};
    std::array<std::array<float, 4>, 8> neutralScriptVectorDefaults_{
        std::array<float,4>{0,0,0,0}, std::array<float,4>{0,0,0,0},
        std::array<float,4>{0,0,0,0}, std::array<float,4>{0,0,0,0},
        std::array<float,4>{0,0,0,0}, std::array<float,4>{0,0,0,0},
        std::array<float,4>{1,0,1,0}, std::array<float,4>{0,0,0,0}
    };
    float cameraYawDegrees_ = 0.0f;
    float cameraPitchDegrees_ = 0.0f;
    float cameraPanX_ = 0.0f;
    float cameraPanY_ = 0.0f;
    float cameraDistance_ = 4.2f;
    float cameraFovDegrees_ = 64.0f;
    float defaultCameraFovDegrees_ = 64.0f;
    std::array<float, 3> backgroundColor_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> environmentAverageColor_{0.72f, 0.77f, 0.84f};
    std::array<float, 3> environmentSunColor_{1.0f, 0.97f, 0.92f};
    fs::path environmentPath_{};
    bool environmentEnabled_ = false;
    bool environmentIsEXR_ = false;
    bool environmentAffectsLighting_ = true;
    bool fulbright_ = false;
    MaterialPreviewProfile materialPreviewProfile_ = MaterialPreviewProfile::LookDev;
    std::array<float, 3> lightColor_{1.0f, 1.0f, 1.0f};
    bool useExplicitLightColor_ = false;
    float environmentRotationDegrees_ = 0.0f;
    float lightYawDegrees_ = 135.0f;
    float lightPitchDegrees_ = 45.0f;
    float lightIntensity_ = 1.2f;
    float ambientIntensity_ = 0.42f;
    float shadowStrength_ = 0.50f;
    float materialUvScaleU_ = 1.0f;
    float materialUvScaleV_ = 1.0f;
    std::array<float, 3> cameraPosition_{0.0f, 0.0f, 0.0f};
    float elapsed_ = 0.0f;
    float lastDelta_ = 0.0f;
    uint64_t frameNumber_ = 0;
    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point fpsWindowStart_{};
    uint32_t fpsWindowFrames_ = 0;
    float previewFps_ = 0.0f;
    float gpuPassMs_ = 0.0f;
    UINT lastViewportWidth_ = 0;
    UINT lastViewportHeight_ = 0;
    ShaderPerformanceStats shaderPerformanceStats_{};
    std::wstring adapterName_{L"Direct3D 11 GPU"};
    std::array<GpuTimerSlot, 4> gpuTimerSlots_{};
    size_t gpuTimerWriteIndex_ = 0;
    bool gpuTimersAvailable_ = false;
    bool gpuTimerActive_ = false;
    uint64_t shaderGeneration_ = 0;

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swapChain_;
    ComPtr<ID3D11RenderTargetView> renderTarget_;
    ComPtr<ID3D11Texture2D> postFxTexture_;
    ComPtr<ID3D11RenderTargetView> postFxRTV_;
    ComPtr<ID3D11ShaderResourceView> postFxSRV_;
    UINT postFxTargetWidth_ = 0;
    UINT postFxTargetHeight_ = 0;
    ComPtr<ID3D11Texture2D> runtimeSceneTexture_;
    ComPtr<ID3D11RenderTargetView> runtimeSceneRTV_;
    ComPtr<ID3D11ShaderResourceView> runtimeSceneSRV_;
    UINT runtimeSceneWidth_ = 0;
    UINT runtimeSceneHeight_ = 0;
    ComPtr<ID3D11VertexShader> postFxVertexShader_;
    ComPtr<ID3D11VertexShader> skyVertexShader_;
    ComPtr<ID3D11VertexShader> materialVertexShader_;
    ComPtr<ID3D11VertexShader> directionalMaterialVertexShader_;
    ComPtr<ID3D11VertexShader> adaptedMaterialVertexShader_;
    ComPtr<ID3D11VertexShader> userVertexShader_;
    ComPtr<ID3D11VertexShader> blitVertexShader_;
    ComPtr<ID3D11InputLayout> materialInputLayout_;
    ComPtr<ID3D11InputLayout> userMaterialInputLayout_;
    ComPtr<ID3D11Buffer> cameraVSBuffer_;
    ComPtr<ID3D11Buffer> materialCameraBuffer_;
    ComPtr<ID3D11Buffer> deferredLightBuffer_;
    ComPtr<ID3D11PixelShader> pixelShader_;
    ComPtr<ID3D11PixelShader> temporalExposureStatePS_;
    bool temporalExposureMode_ = false;
    std::array<ComPtr<ID3D11Texture2D>, 2> temporalExposureTextures_{};
    std::array<ComPtr<ID3D11RenderTargetView>, 2> temporalExposureRTVs_{};
    std::array<ComPtr<ID3D11ShaderResourceView>, 2> temporalExposureSRVs_{};
    int temporalExposureReadIndex_ = 0;
    ComPtr<ID3D11PixelShader> vertexOnlyMaterialPixelShader_;
    ComPtr<ID3D11PixelShader> blitPixelShader_;
    ComPtr<ID3D11PixelShader> runtimeSceneEncodePixelShader_;
    ComPtr<ID3D11PixelShader> runtimeSceneDecodePixelShader_;
    ComPtr<ID3D11Buffer> runtimeSceneParamsBuffer_;
    ComPtr<ID3D11PixelShader> blitDepthPixelShader_;
    ComPtr<ID3D11PixelShader> environmentPixelShader_;
    ComPtr<ID3D11PixelShader> deferredLightPixelShader_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11SamplerState> materialWrapSampler_;
    ComPtr<ID3D11SamplerState> materialColorSampler_;
    ComPtr<ID3D11RasterizerState> materialRasterizerState_;
    ComPtr<ID3D11RasterizerState> materialWireframeRasterizerState_;
    ComPtr<ID3D11RasterizerState> comparisonScissorRasterizer_;
    ComPtr<ID3D11ShaderResourceView> sourceSRV_;
    ComPtr<ID3D11ShaderResourceView> depthSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralDepthSRV_;
    bool depthUserLoaded_ = false;
    bool builtInDepthScene_ = false;
    bool capturedBO3DepthScene_ = false;
    float previewZNear_ = 0.1f;
    ComPtr<ID3D11ShaderResourceView> environmentSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralNormalSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralBlackSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralSpecularSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralGlossSRV_;
    ComPtr<ID3D11ShaderResourceView> neutral1DSRV_;
    ComPtr<ID3D11ShaderResourceView> neutral3DSRV_;
    ComPtr<ID3D11ShaderResourceView> neutralCubeSRV_;
    std::array<ComPtr<ID3D11Texture2D>, 4> gbufferTextures_{};
    std::array<ComPtr<ID3D11RenderTargetView>, 4> gbufferRTVs_{};
    std::array<ComPtr<ID3D11ShaderResourceView>, 4> gbufferSRVs_{};
    std::array<ComPtr<ID3D11ShaderResourceView>, kMaterialTextureSlotCount> materialTextureSRVs_{};
    std::array<fs::path, kMaterialTextureSlotCount> materialTexturePaths_{};
    std::array<UINT, kMaterialTextureSlotCount> materialTextureWidths_{};
    std::array<UINT, kMaterialTextureSlotCount> materialTextureHeights_{};
    std::array<bool, kMaterialTextureSlotCount> materialTextureSrgb_{};
    std::array<UINT, kMaterialTextureSlotCount> materialTextureBindings_{{0,2,3,4,5,6,7,8}};
    std::array<ComPtr<ID3D11ShaderResourceView>, kShadertoyChannelCount> shadertoyChannelSRVs_{};
    std::array<fs::path, kShadertoyChannelCount> shadertoyChannelPaths_{};
    std::array<UINT, kShadertoyChannelCount> shadertoyChannelWidths_{{0,0,0,0}};
    std::array<UINT, kShadertoyChannelCount> shadertoyChannelHeights_{{0,0,0,0}};
    std::array<bool, kShadertoyChannelCount> shadertoyChannelRepeat_{{true,true,true,true}};
    std::array<bool, kShadertoyChannelCount> shadertoyChannelFlipY_{{true,true,true,true}};
    ComPtr<ID3D11Texture2D> materialDepthTexture_;
    ComPtr<ID3D11DepthStencilView> materialDepthDSV_;
    ComPtr<ID3D11ShaderResourceView> materialDepthSRV_;
    PreviewMeshBuffers sphereMesh_;
    PreviewMeshBuffers cubeMesh_;
    PreviewMeshBuffers planeMesh_;
    PreviewMeshBuffers cardMesh_;
    PreviewMeshBuffers apeSphereMesh_;
    PreviewMeshBuffers apeCubeMesh_;
    PreviewMeshBuffers apePlaneMesh_;
    PreviewMeshBuffers customMesh_;
    fs::path customModelPath_;
    std::wstring customModelFormat_;
    UINT customModelVertexCount_ = 0;
    std::vector<ReflectedCBuffer> cbuffers_;
    std::vector<ReflectedResource> resources_;
    PreviewMode previewMode_ = PreviewMode::HLSL;
    DisplayFitMode displayFitMode_ = DisplayFitMode::Fill;
    PostFxPreviewContext postFxPreviewContext_ = PostFxPreviewContext::Bo3RuntimeResolvedScene;
    GBufferView gbufferView_ = GBufferView::Final;
    PreviewMesh previewMesh_ = PreviewMesh::Sphere;
    float lookdevExposureEV_ = 0.0f;
    int toneMapMode_ = 2; // ACES
    bool groundEnabled_ = true;
    float contactShadowStrength_ = 0.55f;
    bool wireframe_ = false;
};

PreviewRenderer::PreviewRenderer() : impl_(std::make_unique<Impl>()) {}
PreviewRenderer::~PreviewRenderer() = default;
PreviewRenderer::PreviewRenderer(PreviewRenderer&&) noexcept = default;
PreviewRenderer& PreviewRenderer::operator=(PreviewRenderer&&) noexcept = default;

bool PreviewRenderer::Initialize(HWND hwnd, std::wstring& error)
{
    return impl_->Initialize(hwnd, error);
}

void PreviewRenderer::Resize(UINT w, UINT h)
{
    impl_->Resize(w, h);
}

bool PreviewRenderer::CompilePixelShader(const std::string& userSource, const std::filesystem::path& sourcePath, const std::filesystem::path& includeRoot, const std::string& entryPoint, const std::string& profile, bool injectBO3Globals, std::wstring& errors)
{
    return impl_->CompilePixelShader(userSource, sourcePath, includeRoot, entryPoint, profile, injectBO3Globals, errors);
}

bool PreviewRenderer::LoadEnvironmentTexture(const std::filesystem::path& path, std::wstring& error)
{
    return impl_->LoadEnvironmentTexture(path, error);
}

bool PreviewRenderer::LoadEnvironmentCubemapFaces(const std::array<std::filesystem::path, 6>& faces, std::wstring& error, UINT outputWidth, UINT outputHeight)
{
    return impl_->LoadEnvironmentCubemapFaces(faces, error, outputWidth, outputHeight);
}

bool PreviewRenderer::CreateDefaultStudioEnvironment(std::wstring& error)
{
    return impl_->CreateDefaultStudioEnvironment(error);
}

void PreviewRenderer::ClearEnvironmentTexture()
{
    impl_->ClearEnvironmentTexture();
}

bool PreviewRenderer::EnvironmentEnabled() const
{
    return impl_->EnvironmentEnabled();
}

bool PreviewRenderer::EnvironmentIsEXR() const
{
    return impl_->EnvironmentIsEXR();
}

std::wstring PreviewRenderer::EnvironmentPath() const
{
    return impl_->EnvironmentPath();
}

bool PreviewRenderer::BakeCurrentSkyToEXR(const std::filesystem::path& outputPath, int outputWidth, int outputHeight, std::wstring& error)
{
    return impl_->BakeCurrentSkyToEXR(outputPath, outputWidth, outputHeight, error);
}

void PreviewRenderer::SetEnvironmentAffectsLighting(bool enabled)
{
    impl_->SetEnvironmentAffectsLighting(enabled);
}

bool PreviewRenderer::EnvironmentAffectsLighting() const
{
    return impl_->EnvironmentAffectsLighting();
}

void PreviewRenderer::SetFulbright(bool enabled)
{
    impl_->SetFulbright(enabled);
}

bool PreviewRenderer::Fulbright() const
{
    return impl_->Fulbright();
}

void PreviewRenderer::SetMaterialPreviewProfile(MaterialPreviewProfile profile)
{
    impl_->SetMaterialPreviewProfile(profile);
}

MaterialPreviewProfile PreviewRenderer::GetMaterialPreviewProfile() const
{
    return impl_->GetMaterialPreviewProfile();
}

void PreviewRenderer::SetLightColor(float r, float g, float b)
{
    impl_->SetLightColor(r, g, b);
}

std::array<float,3> PreviewRenderer::LightColor() const
{
    return impl_->LightColor();
}

void PreviewRenderer::ResetLightColorToEnvironment()
{
    impl_->ResetLightColorToEnvironment();
}

void PreviewRenderer::SetEnvironmentRotationDegrees(float degrees)
{
    impl_->SetEnvironmentRotationDegrees(degrees);
}

float PreviewRenderer::EnvironmentRotationDegrees() const
{
    return impl_->EnvironmentRotationDegrees();
}

bool PreviewRenderer::LoadShadertoyChannelTexture(int channel, const std::filesystem::path& path, bool flipY, std::wstring& error)
{
    return impl_->LoadShadertoyChannelTexture(channel, path, flipY, error);
}

void PreviewRenderer::ClearShadertoyChannelTexture(int channel)
{
    impl_->ClearShadertoyChannelTexture(channel);
}

void PreviewRenderer::SetShadertoyChannelRepeat(int channel, bool repeat)
{
    impl_->SetShadertoyChannelRepeat(channel, repeat);
}

bool PreviewRenderer::GetShadertoyChannelRepeat(int channel) const
{
    return impl_->GetShadertoyChannelRepeat(channel);
}

bool PreviewRenderer::GetShadertoyChannelFlipY(int channel) const
{
    return impl_->GetShadertoyChannelFlipY(channel);
}

std::wstring PreviewRenderer::GetShadertoyChannelPath(int channel) const
{
    return impl_->GetShadertoyChannelPath(channel);
}

UINT PreviewRenderer::GetShadertoyChannelWidth(int channel) const
{
    return impl_->GetShadertoyChannelWidth(channel);
}

UINT PreviewRenderer::GetShadertoyChannelHeight(int channel) const
{
    return impl_->GetShadertoyChannelHeight(channel);
}

std::vector<int> PreviewRenderer::RequiredShadertoyChannels() const
{
    return impl_->RequiredShadertoyChannels();
}

std::vector<int> PreviewRenderer::MissingShadertoyChannels() const
{
    return impl_->MissingShadertoyChannels();
}

bool PreviewRenderer::LoadMaterialTexture(int logicalSlot, const std::filesystem::path& path, std::wstring& error)
{
    return impl_->LoadMaterialTexture(logicalSlot, path, error);
}

void PreviewRenderer::ClearMaterialTexture(int logicalSlot)
{
    impl_->ClearMaterialTexture(logicalSlot);
}

void PreviewRenderer::SetMaterialTextureBinding(int logicalSlot, UINT bindSlot)
{
    impl_->SetMaterialTextureBinding(logicalSlot, bindSlot);
}

UINT PreviewRenderer::GetMaterialTextureBinding(int logicalSlot) const
{
    return impl_->GetMaterialTextureBinding(logicalSlot);
}

std::wstring PreviewRenderer::GetMaterialTexturePath(int logicalSlot) const
{
    return impl_->GetMaterialTexturePath(logicalSlot);
}

UINT PreviewRenderer::GetMaterialTextureWidth(int logicalSlot) const
{
    return impl_->GetMaterialTextureWidth(logicalSlot);
}

UINT PreviewRenderer::GetMaterialTextureHeight(int logicalSlot) const
{
    return impl_->GetMaterialTextureHeight(logicalSlot);
}

bool PreviewRenderer::GetMaterialTextureIsSrgb(int logicalSlot) const
{
    return impl_->GetMaterialTextureIsSrgb(logicalSlot);
}

void PreviewRenderer::SetMaterialUvScale(float u, float v)
{
    impl_->SetMaterialUvScale(u, v);
}

float PreviewRenderer::MaterialUvScaleU() const
{
    return impl_->MaterialUvScaleU();
}

float PreviewRenderer::MaterialUvScaleV() const
{
    return impl_->MaterialUvScaleV();
}

void PreviewRenderer::ResetMaterialUvScale()
{
    impl_->ResetMaterialUvScale();
}

void PreviewRenderer::ResetTemporalExposureHistory()
{
    impl_->ResetTemporalExposureHistory();
}

bool PreviewRenderer::TemporalExposureActive() const
{
    return impl_->TemporalExposureActive();
}

bool PreviewRenderer::StartLiveCapture(HWND target, std::wstring& error)
{
    return impl_->StartLiveCapture(target, error);
}

void PreviewRenderer::StopLiveCapture()
{
    impl_->StopLiveCapture();
}

bool PreviewRenderer::LiveCaptureActive() const
{
    return impl_->LiveCaptureActive();
}

bool PreviewRenderer::IsLiveCaptureSource() const
{
    return impl_->IsLiveCaptureSource();
}

bo3::LiveCaptureDiagnostics PreviewRenderer::LiveCaptureInfo() const
{
    return impl_->LiveCaptureInfo();
}

const QString& PreviewRenderer::LiveCaptureNotification() const
{
    return impl_->LiveCaptureNotification();
}

void PreviewRenderer::SetLiveComparisonMode(LiveComparisonMode mode)
{
    impl_->SetLiveComparisonMode(mode);
}

LiveComparisonMode PreviewRenderer::GetLiveComparisonMode() const
{
    return impl_->GetLiveComparisonMode();
}

void PreviewRenderer::SetLiveSplitFraction(float value)
{
    impl_->SetLiveSplitFraction(value);
}

float PreviewRenderer::LiveSplitFraction() const
{
    return impl_->LiveSplitFraction();
}

bool PreviewRenderer::LoadTexture(const std::filesystem::path& path, bool depth, std::wstring& error)
{
    return impl_->LoadTexture(path, depth, error);
}

bool PreviewRenderer::UseBuiltInDepthScene(std::wstring& error, const QString& sceneId)
{
    return impl_->UseBuiltInDepthScene(error, sceneId);
}

bool PreviewRenderer::HasUserDepthTexture() const
{
    return impl_->HasUserDepthTexture();
}

bool PreviewRenderer::HasPreviewDepthTexture() const
{
    return impl_->HasPreviewDepthTexture();
}

bool PreviewRenderer::BuiltInDepthSceneActive() const
{
    return impl_->BuiltInDepthSceneActive();
}

bool PreviewRenderer::CapturedBO3DepthSceneActive() const
{
    return impl_->CapturedBO3DepthSceneActive();
}

float PreviewRenderer::PreviewZNear() const
{
    return impl_->PreviewZNear();
}

void PreviewRenderer::SetPaused(bool paused)
{
    impl_->SetPaused(paused);
}

void PreviewRenderer::SetShaderDrivenMovementDisabled(bool disabled)
{
    impl_->SetShaderDrivenMovementDisabled(disabled);
}

bool PreviewRenderer::ShaderDrivenMovementDisabled() const
{
    return impl_->ShaderDrivenMovementDisabled();
}

void PreviewRenderer::SetBO3NeutralDefaults(bool enabled)
{
    impl_->SetBO3NeutralDefaults(enabled);
}

std::vector<EditableShaderParameter> PreviewRenderer::EditableShaderParameters() const
{
    return impl_->EditableShaderParameters();
}

void PreviewRenderer::SetShaderParameter(const std::string& name, float value)
{
    impl_->SetShaderParameter(name, value);
}

void PreviewRenderer::ResetShaderParameters()
{
    impl_->ResetShaderParameters();
}

const std::array<bool, 8>& PreviewRenderer::DetectedScriptVectors() const
{
    return impl_->DetectedScriptVectors();
}

std::array<float, 4> PreviewRenderer::GetEffectiveScriptVector(int index) const
{
    return impl_->GetEffectiveScriptVector(index);
}

void PreviewRenderer::SetScriptVectorComponent(int index, int component, float value)
{
    impl_->SetScriptVectorComponent(index, component, value);
}

void PreviewRenderer::ResetScriptVectorOverrides()
{
    impl_->ResetScriptVectorOverrides();
}

void PreviewRenderer::SetNeutralScriptVector(int index, const std::array<float, 4>& value)
{
    impl_->SetNeutralScriptVector(index, value);
}

void PreviewRenderer::SetPreviewMode(PreviewMode mode)
{
    impl_->SetPreviewMode(mode);
}

PreviewMode PreviewRenderer::GetPreviewMode() const
{
    return impl_->GetPreviewMode();
}

bool PreviewRenderer::MaterialUsesDeferredGBuffer() const
{
    return impl_->MaterialUsesDeferredGBuffer();
}

void PreviewRenderer::SetDisplayFitMode(DisplayFitMode mode)
{
    impl_->SetDisplayFitMode(mode);
}

DisplayFitMode PreviewRenderer::GetDisplayFitMode() const
{
    return impl_->GetDisplayFitMode();
}

void PreviewRenderer::SetPostFxPreviewContext(PostFxPreviewContext context)
{
    impl_->SetPostFxPreviewContext(context);
}

PostFxPreviewContext PreviewRenderer::GetPostFxPreviewContext() const
{
    return impl_->GetPostFxPreviewContext();
}

void PreviewRenderer::SetPackagePreviewSuppressed(bool suppressed)
{
    impl_->SetPackagePreviewSuppressed(suppressed);
}

bool PreviewRenderer::IsPackagePreviewSuppressed() const
{
    return impl_->IsPackagePreviewSuppressed();
}

void PreviewRenderer::SetPreviewResourceMappings(const QVector<bo3::PackageResourceMapping>& mappings)
{
    impl_->SetPreviewResourceMappings(mappings);
}

void PreviewRenderer::ClearPreviewResourceMappings()
{
    impl_->ClearPreviewResourceMappings();
}

QSet<QString> PreviewRenderer::OptimizedPixelResourceNames() const
{
    return impl_->OptimizedPixelResourceNames();
}

bool PreviewRenderer::OptimizedPixelResourceReflectionValid() const
{
    return impl_->OptimizedPixelResourceReflectionValid();
}

void PreviewRenderer::SetPostFxRuntimeExposureEV(float ev)
{
    impl_->SetPostFxRuntimeExposureEV(ev);
}

float PreviewRenderer::GetPostFxRuntimeExposureEV() const
{
    return impl_->GetPostFxRuntimeExposureEV();
}

bool PreviewRenderer::SourceIsLinearHDR() const
{
    return impl_->SourceIsLinearHDR();
}

float PreviewRenderer::SourceHdrPeakLuminance() const
{
    return impl_->SourceHdrPeakLuminance();
}

float PreviewRenderer::SourceHdrMeanLuminance() const
{
    return impl_->SourceHdrMeanLuminance();
}

void PreviewRenderer::SetPreviewMesh(PreviewMesh mesh)
{
    impl_->SetPreviewMesh(mesh);
}

PreviewMesh PreviewRenderer::GetPreviewMesh() const
{
    return impl_->GetPreviewMesh();
}

bool PreviewRenderer::LoadCustomModel(const std::filesystem::path& path, std::wstring& error)
{
    return impl_->LoadCustomModel(path, error);
}

bool PreviewRenderer::LoadApeReferenceMesh(PreviewMesh mesh, const std::filesystem::path& path, std::wstring& error)
{
    return impl_->LoadApeReferenceMesh(mesh, path, error);
}

void PreviewRenderer::ClearApeReferenceMeshes()
{
    impl_->ClearApeReferenceMeshes();
}

bool PreviewRenderer::HasApeReferenceMesh(PreviewMesh mesh) const
{
    return impl_->HasApeReferenceMesh(mesh);
}

void PreviewRenderer::ClearCustomModel()
{
    impl_->ClearCustomModel();
}

bool PreviewRenderer::HasCustomModel() const
{
    return impl_->HasCustomModel();
}

std::wstring PreviewRenderer::CustomModelPath() const
{
    return impl_->CustomModelPath();
}

std::wstring PreviewRenderer::CustomModelFormat() const
{
    return impl_->CustomModelFormat();
}

UINT PreviewRenderer::CustomModelVertexCount() const
{
    return impl_->CustomModelVertexCount();
}

UINT PreviewRenderer::CustomModelTriangleCount() const
{
    return impl_->CustomModelTriangleCount();
}

void PreviewRenderer::SetLookdevExposureEV(float ev)
{
    impl_->SetLookdevExposureEV(ev);
}

float PreviewRenderer::LookdevExposureEV() const
{
    return impl_->LookdevExposureEV();
}

void PreviewRenderer::SetToneMapMode(int mode)
{
    impl_->SetToneMapMode(mode);
}

int PreviewRenderer::ToneMapMode() const
{
    return impl_->ToneMapMode();
}

void PreviewRenderer::SetGroundEnabled(bool enabled)
{
    impl_->SetGroundEnabled(enabled);
}

bool PreviewRenderer::GroundEnabled() const
{
    return impl_->GroundEnabled();
}

void PreviewRenderer::SetContactShadowStrength(float value)
{
    impl_->SetContactShadowStrength(value);
}

float PreviewRenderer::ContactShadowStrength() const
{
    return impl_->ContactShadowStrength();
}

void PreviewRenderer::SetWireframe(bool enabled)
{
    impl_->SetWireframe(enabled);
}

bool PreviewRenderer::Wireframe() const
{
    return impl_->Wireframe();
}

void PreviewRenderer::SetGBufferView(GBufferView view)
{
    impl_->SetGBufferView(view);
}

GBufferView PreviewRenderer::GetGBufferView() const
{
    return impl_->GetGBufferView();
}

bool PreviewRenderer::IsSkyShaderMode() const
{
    return impl_->IsSkyShaderMode();
}

bool PreviewRenderer::IsVertexOnlyShader() const
{
    return impl_->IsVertexOnlyShader();
}

void PreviewRenderer::RotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees)
{
    impl_->RotateCamera(yawDeltaDegrees, pitchDeltaDegrees);
}

void PreviewRenderer::AdjustCameraFov(float deltaDegrees)
{
    impl_->AdjustCameraFov(deltaDegrees);
}

void PreviewRenderer::ResetCamera()
{
    impl_->ResetCamera();
}

void PreviewRenderer::SetDefaultCameraFov(float fovDegrees)
{
    impl_->SetDefaultCameraFov(fovDegrees);
}

float PreviewRenderer::CameraYawDegrees() const
{
    return impl_->CameraYawDegrees();
}

float PreviewRenderer::CameraPitchDegrees() const
{
    return impl_->CameraPitchDegrees();
}

float PreviewRenderer::CameraFovDegrees() const
{
    return impl_->CameraFovDegrees();
}

float PreviewRenderer::CameraPanX() const
{
    return impl_->CameraPanX();
}

float PreviewRenderer::CameraPanY() const
{
    return impl_->CameraPanY();
}

float PreviewRenderer::CameraDistance() const
{
    return impl_->CameraDistance();
}

void PreviewRenderer::PanCamera(float dx, float dy)
{
    impl_->PanCamera(dx, dy);
}

void PreviewRenderer::RotateLight(float yawDeltaDegrees, float pitchDeltaDegrees)
{
    impl_->RotateLight(yawDeltaDegrees, pitchDeltaDegrees);
}

void PreviewRenderer::SetBackgroundColor(float r, float g, float b)
{
    impl_->SetBackgroundColor(r, g, b);
}

std::array<float,3> PreviewRenderer::BackgroundColor() const
{
    return impl_->BackgroundColor();
}

void PreviewRenderer::ResetBackgroundColor()
{
    impl_->ResetBackgroundColor();
}

void PreviewRenderer::SetLightAngles(float yawDeg, float pitchDeg)
{
    impl_->SetLightAngles(yawDeg, pitchDeg);
}

float PreviewRenderer::LightYawDegrees() const
{
    return impl_->LightYawDegrees();
}

float PreviewRenderer::LightPitchDegrees() const
{
    return impl_->LightPitchDegrees();
}

void PreviewRenderer::SetLightIntensity(float v)
{
    impl_->SetLightIntensity(v);
}

float PreviewRenderer::LightIntensity() const
{
    return impl_->LightIntensity();
}

void PreviewRenderer::SetAmbientIntensity(float v)
{
    impl_->SetAmbientIntensity(v);
}

float PreviewRenderer::AmbientIntensity() const
{
    return impl_->AmbientIntensity();
}

void PreviewRenderer::SetShadowStrength(float v)
{
    impl_->SetShadowStrength(v);
}

float PreviewRenderer::ShadowStrength() const
{
    return impl_->ShadowStrength();
}

float PreviewRenderer::PreviewFps() const
{
    return impl_->PreviewFps();
}

float PreviewRenderer::GpuPassMs() const
{
    return impl_->GpuPassMs();
}

bool PreviewRenderer::HasGpuTiming() const
{
    return impl_->HasGpuTiming();
}

bool PreviewRenderer::GpuTimingSupported() const
{
    return impl_->GpuTimingSupported();
}

UINT PreviewRenderer::LastViewportWidth() const
{
    return impl_->LastViewportWidth();
}

UINT PreviewRenderer::LastViewportHeight() const
{
    return impl_->LastViewportHeight();
}

const ShaderPerformanceStats& PreviewRenderer::PerformanceStats() const
{
    return impl_->PerformanceStats();
}

const std::wstring& PreviewRenderer::AdapterName() const
{
    return impl_->AdapterName();
}

float PreviewRenderer::EstimatedGpuPassMs(UINT targetWidth, UINT targetHeight) const
{
    return impl_->EstimatedGpuPassMs(targetWidth, targetHeight);
}

void PreviewRenderer::Render()
{
    impl_->Render();
}

