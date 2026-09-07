// Regression: GLSL source may define a helper macro named `mul`, which collides
// with HLSL's matrix multiplication intrinsic after conversion. The converter
// must rename the user macro before emitting native HLSL mul() calls.
#define mul(a,b) ((a)*(b))

vec2 component_mul(vec2 a, vec2 b)
{
    return mul(a, b);
}

vec2 complex_div_like(vec2 a, vec2 b)
{
    return a * mat2(b, -b.y, b.x);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec2 a = component_mul(uv, vec2(0.75, 0.25));
    vec2 b = complex_div_like(a, vec2(0.5, 0.125));
    fragColor = vec4(b, 0.0, 1.0);
}
