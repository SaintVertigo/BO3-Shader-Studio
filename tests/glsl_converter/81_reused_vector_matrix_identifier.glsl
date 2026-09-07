// A matrix parameter named x elsewhere must not poison the vec3 x below.
mat3 matrixIdentity(mat3 x)
{
    return x;
}

vec3 mod289_scope(vec3 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

mat3 mod289_scope(mat3 x)
{
    return x;
}

vec3 permuteScope(vec3 x)
{
    // This is component-wise vector multiplication in GLSL/HLSL, not matrix mul().
    return mod289_scope((34.0 * x + 1.0) * x);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 p = permuteScope(vec3(uv, 0.25));
    fragColor = vec4(p, 1.0);
}
