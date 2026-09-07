// BO3_EXPECT_NOTE_CONTAINS: Expanded Shadertoy builtin object-alias macro(s)
// BO3_EXPECT_NOTE_CONTAINS: parameter-shadowed Shadertoy built-ins
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: #define t iTime
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: in float iTime
// BO3_EXPECT_CONVERTED_CONTAINS: in float _glsl_local_iTime_

#define t iTime

float fnoise(vec3 p, in float t)
{
    p *= .25;
    p.y -= t * .1;
    return p.y;
}

float cloud(vec3 p, in float t)
{
    return fnoise(p, t) + t * .01;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    float v = cloud(vec3(uv, 1.0), t);
    fragColor = vec4(vec3(v), 1.0);
}
