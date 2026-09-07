#include "hlsl_preview_mode.h"

namespace bo3
{

PreviewPolicy previewPolicy(ShaderPreviewMode mode)
{
    PreviewPolicy policy;
    switch(mode)
    {
        case ShaderPreviewMode::Hlsl:
            policy.allowSyntheticGlobals = true;
            policy.packageStatus = "N/A";
            policy.description =
                "HLSL Preview — Generic Direct3D shader preview. "
                "Rendering here does not prove BO3 package compatibility.";
            return policy;
        case ShaderPreviewMode::PostFx:
            policy.enforcePackage = true;
            policy.previewWithoutPackage = false;
            policy.packageStatus = "NOT RUN";
            policy.description = "BO3 PostFX — strict resolved HLSL + techset package preview.";
            return policy;
        case ShaderPreviewMode::Material:
            policy.enforcePackage = true;
            policy.previewWithoutPackage = false;
            policy.packageStatus = "NOT RUN";
            policy.description = "BO3 Material — strict geometry/material package preview.";
            return policy;
        case ShaderPreviewMode::Skybox:
            policy.enforcePackage = true;
            policy.previewWithoutPackage = false;
            policy.packageStatus = "NOT RUN";
            policy.description = "BO3 Skybox — strict sky package preview.";
            return policy;
    }
    return policy;
}

QString toString(ShaderPreviewMode mode)
{
    switch(mode)
    {
        case ShaderPreviewMode::Hlsl: return "HLSL";
        case ShaderPreviewMode::PostFx: return "PostFX";
        case ShaderPreviewMode::Material: return "Material";
        case ShaderPreviewMode::Skybox: return "Skybox";
    }
    return "HLSL";
}

} // namespace bo3
