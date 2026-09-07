float z(float x)
{
    return x * 2.0;
}

float shadowSwizzle(vec3 p)
{
    float z = z(p.x);
    z += p.z;
    z += p . z;
    return z;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec3 p = vec3(uv, 0.25);
    float v = shadowSwizzle(p);
    fragColor = vec4(v, p.z, uv.x, 1.0);
}
