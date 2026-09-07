#pragma once

#include <QString>

namespace bo3
{

enum class ShaderPreviewMode
{
    Hlsl,
    PostFx,
    Material,
    Skybox
};

struct PreviewPolicy
{
    bool enforcePackage = false;
    bool allowSyntheticGlobals = false;
    bool previewWithoutPackage = true;
    QString packageStatus;
    QString description;
};

// Neutral identity exposure. Higher values remain available as an explicit
// user calibration, but no empirical scene fit is treated as BO3 parity.
constexpr float kDefaultBo3RuntimeSceneExposureEv = 0.0f;

PreviewPolicy previewPolicy(ShaderPreviewMode mode);
QString toString(ShaderPreviewMode mode);

} // namespace bo3
