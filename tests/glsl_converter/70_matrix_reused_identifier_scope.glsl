// Regression: the same short identifier is scalar in one function and vector in another.
// The vector * inline-matrix expression must still lower to HLSL mul().
float scalar_identity(float a)
{
    return a;
}

vec2 complex_div_like(vec2 a, vec2 b)
{
    return a * mat2(b, -b.y, b.x);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;
    vec2 v = complex_div_like(uv, vec2(0.75, 0.25));
    fragColor = vec4(v, scalar_identity(0.5), 1.0);
}
