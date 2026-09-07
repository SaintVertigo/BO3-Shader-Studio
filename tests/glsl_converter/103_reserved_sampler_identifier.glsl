// BO3_EXPECT_NOTE_CONTAINS: sampler
// BO3_EXPECT_CONVERTED_CONTAINS: Texture2D<float4> _glsl_hlsl_sampler
// BO3_EXPECT_CONVERTED_CONTAINS: GLSL_TEXTURE_SIZE(_glsl_hlsl_sampler, 0)
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: Texture2D<float4> sampler

vec4 sampleSizedTexture(sampler2D sampler, vec2 uv)
{
    ivec2 size = textureSize(sampler, 0);
    return texture(sampler, uv / vec2(size));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = sampleSizedTexture(iChannel0, fragCoord);
}
