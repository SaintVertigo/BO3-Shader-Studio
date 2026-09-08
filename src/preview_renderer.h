#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <QSet>
#include <QString>
#include <QVector>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "bo3_package_adapter.h"
#include "live_window_capture.h"

enum class PreviewMode
{
    HLSL = 0,
    PostFX = 1,
    Sky = 2,
    ForwardMaterial = 3,
    DeferredGBuffer = 4
};

enum class PostFxPreviewContext
{
    ToolsgfxMaterial = 0,
    Bo3RuntimeResolvedScene = 1
};

enum class PreviewSourceEncoding
{
    LdrSrgb = 0,
    HdrSceneLinear = 1
};

enum class LiveComparisonMode
{
    ProcessedOnly = 0,
    SideBySide = 1,
    Split = 2
};

enum class GBufferView
{
    Final = 0,
    RT0,
    RT1,
    RT2,
    RT3,
    Depth,
    Albedo,
    Normal,
    Specular,
    Gloss,
    AO,
    Emissive
};

enum class DisplayFitMode
{
    Fill = 0,
    Fit = 1
};

enum class PreviewMesh
{
    Sphere = 0,
    Cube = 1,
    Plane = 2,
    Card = 3,
    Custom = 4
};

constexpr int kMaterialTextureSlotCount = 8;
constexpr int kShadertoyChannelCount = 4;

struct EditableShaderParameter
{
    std::string name;
    bool isBool = false;
    float value = 0.0f;
};

struct ShaderPerformanceStats
{
    UINT instructionCount = 0;
    UINT tempRegisterCount = 0;
    UINT textureNormalInstructions = 0;
    UINT textureLoadInstructions = 0;
    UINT textureCompInstructions = 0;
    UINT textureBiasInstructions = 0;
    UINT textureGradientInstructions = 0;
    UINT dynamicFlowControlCount = 0;

    UINT TextureInstructionCount() const
    {
        return textureNormalInstructions + textureLoadInstructions + textureCompInstructions +
               textureBiasInstructions + textureGradientInstructions;
    }
};

class PreviewRenderer
{
public:
    PreviewRenderer();
    ~PreviewRenderer();
    PreviewRenderer(const PreviewRenderer&) = delete;
    PreviewRenderer& operator=(const PreviewRenderer&) = delete;
    PreviewRenderer(PreviewRenderer&&) noexcept;
    PreviewRenderer& operator=(PreviewRenderer&&) noexcept;

    bool Initialize(HWND hwnd, std::wstring& error);
    void Resize(UINT w, UINT h);
    bool CompilePixelShader(const std::string& userSource, const std::filesystem::path& sourcePath, const std::filesystem::path& includeRoot, const std::string& entryPoint, const std::string& profile, bool injectBO3Globals, std::wstring& errors);
    bool LoadEnvironmentTexture(const std::filesystem::path& path, std::wstring& error);
    bool CreateDefaultStudioEnvironment(std::wstring& error);
    void ClearEnvironmentTexture();
    bool EnvironmentEnabled() const;
    bool EnvironmentIsEXR() const;
    std::wstring EnvironmentPath() const;
    bool BakeCurrentSkyToEXR(const std::filesystem::path& outputPath, int outputWidth, int outputHeight, std::wstring& error);
    void SetEnvironmentAffectsLighting(bool enabled);
    bool EnvironmentAffectsLighting() const;
    void SetFulbright(bool enabled);
    bool Fulbright() const;
    bool LoadShadertoyChannelTexture(int channel, const std::filesystem::path& path, bool flipY, std::wstring& error);
    void ClearShadertoyChannelTexture(int channel);
    void SetShadertoyChannelRepeat(int channel, bool repeat);
    bool GetShadertoyChannelRepeat(int channel) const;
    bool GetShadertoyChannelFlipY(int channel) const;
    std::wstring GetShadertoyChannelPath(int channel) const;
    UINT GetShadertoyChannelWidth(int channel) const;
    UINT GetShadertoyChannelHeight(int channel) const;
    std::vector<int> RequiredShadertoyChannels() const;
    std::vector<int> MissingShadertoyChannels() const;
    bool LoadMaterialTexture(int logicalSlot, const std::filesystem::path& path, std::wstring& error);
    void ClearMaterialTexture(int logicalSlot);
    void SetMaterialTextureBinding(int logicalSlot, UINT bindSlot);
    UINT GetMaterialTextureBinding(int logicalSlot) const;
    std::wstring GetMaterialTexturePath(int logicalSlot) const;
    void SetMaterialUvScale(float u, float v);
    float MaterialUvScaleU() const;
    float MaterialUvScaleV() const;
    void ResetMaterialUvScale();
    void ResetTemporalExposureHistory();
    bool TemporalExposureActive() const;
    bool StartLiveCapture(HWND target, std::wstring& error);
    void StopLiveCapture();
    bool LiveCaptureActive() const;
    bool IsLiveCaptureSource() const;
    bo3::LiveCaptureDiagnostics LiveCaptureInfo() const;
    const QString& LiveCaptureNotification() const;
    void SetLiveComparisonMode(LiveComparisonMode mode);
    LiveComparisonMode GetLiveComparisonMode() const;
    void SetLiveSplitFraction(float value);
    float LiveSplitFraction() const;
    bool LoadTexture(const std::filesystem::path& path, bool depth, std::wstring& error);
    bool UseBuiltInDepthScene(std::wstring& error);
    bool HasUserDepthTexture() const;
    bool HasPreviewDepthTexture() const;
    bool BuiltInDepthSceneActive() const;
    void SetPaused(bool paused);
    void SetShaderDrivenMovementDisabled(bool disabled);
    bool ShaderDrivenMovementDisabled() const;
    void SetBO3NeutralDefaults(bool enabled);
    std::vector<EditableShaderParameter> EditableShaderParameters() const;
    void SetShaderParameter(const std::string& name, float value);
    void ResetShaderParameters();
    const std::array<bool, 8>& DetectedScriptVectors() const;
    std::array<float, 4> GetEffectiveScriptVector(int index) const;
    void SetScriptVectorComponent(int index, int component, float value);
    void ResetScriptVectorOverrides();
    void SetNeutralScriptVector(int index, const std::array<float, 4>& value);
    void SetPreviewMode(PreviewMode mode);
    PreviewMode GetPreviewMode() const;
    bool MaterialUsesDeferredGBuffer() const;
    void SetDisplayFitMode(DisplayFitMode mode);
    DisplayFitMode GetDisplayFitMode() const;
    void SetPostFxPreviewContext(PostFxPreviewContext context);
    PostFxPreviewContext GetPostFxPreviewContext() const;
    void SetPackagePreviewSuppressed(bool suppressed);
    bool IsPackagePreviewSuppressed() const;
    void SetPreviewResourceMappings(const QVector<bo3::PackageResourceMapping>& mappings);
    void ClearPreviewResourceMappings();
    QSet<QString> OptimizedPixelResourceNames() const;
    bool OptimizedPixelResourceReflectionValid() const;
    void SetPostFxRuntimeExposureEV(float ev);
    float GetPostFxRuntimeExposureEV() const;
    bool SourceIsLinearHDR() const;
    float SourceHdrPeakLuminance() const;
    float SourceHdrMeanLuminance() const;
    void SetPreviewMesh(PreviewMesh mesh);
    PreviewMesh GetPreviewMesh() const;
    bool LoadCustomModel(const std::filesystem::path& path, std::wstring& error);
    void ClearCustomModel();
    bool HasCustomModel() const;
    std::wstring CustomModelPath() const;
    std::wstring CustomModelFormat() const;
    UINT CustomModelVertexCount() const;
    UINT CustomModelTriangleCount() const;
    void SetLookdevExposureEV(float ev);
    float LookdevExposureEV() const;
    void SetToneMapMode(int mode);
    int ToneMapMode() const;
    void SetGroundEnabled(bool enabled);
    bool GroundEnabled() const;
    void SetContactShadowStrength(float value);
    float ContactShadowStrength() const;
    void SetWireframe(bool enabled);
    bool Wireframe() const;
    void SetGBufferView(GBufferView view);
    GBufferView GetGBufferView() const;
    bool IsSkyShaderMode() const;
    bool IsVertexOnlyShader() const;
    void RotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees);
    void AdjustCameraFov(float deltaDegrees);
    void ResetCamera();
    void SetDefaultCameraFov(float fovDegrees);
    float CameraYawDegrees() const;
    float CameraPitchDegrees() const;
    float CameraFovDegrees() const;
    float CameraPanX() const;
    float CameraPanY() const;
    float CameraDistance() const;
    void PanCamera(float dx, float dy);
    void RotateLight(float yawDeltaDegrees, float pitchDeltaDegrees);
    void SetBackgroundColor(float r, float g, float b);
    std::array<float,3> BackgroundColor() const;
    void ResetBackgroundColor();
    void SetLightAngles(float yawDeg, float pitchDeg);
    float LightYawDegrees() const;
    float LightPitchDegrees() const;
    void SetLightIntensity(float v);
    float LightIntensity() const;
    void SetAmbientIntensity(float v);
    float AmbientIntensity() const;
    void SetShadowStrength(float v);
    float ShadowStrength() const;
    float PreviewFps() const;
    float GpuPassMs() const;
    bool HasGpuTiming() const;
    bool GpuTimingSupported() const;
    UINT LastViewportWidth() const;
    UINT LastViewportHeight() const;
    const ShaderPerformanceStats& PerformanceStats() const;
    const std::wstring& AdapterName() const;
    float EstimatedGpuPassMs(UINT targetWidth, UINT targetHeight) const;
    void Render();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
