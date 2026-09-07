float pf(float x)
{
    return x * 2.0;
}

// A macro may intentionally reference a local whose name also exists as a
// function elsewhere in the shader. The local must not be renamed unless its
// own initializer actually needs the same-named function.
#define READ_LOCAL_PF() (pf + 1.0)

float macroLocalShadow()
{
    float pf = 2.0;
    return READ_LOCAL_PF();
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float v = macroLocalShadow();
    fragColor = vec4(v, fragCoord.x / iResolution.x, 0.0, 1.0);
}
