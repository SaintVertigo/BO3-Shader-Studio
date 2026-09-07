// BO3_EXPECT_NOTE_CONTAINS: macro-safe explicit matrix multiplication
// BO3_EXPECT_CONVERTED_CONTAINS: mul(float2x2(
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: (p)*float2x2

#define HASH100(p) fract(sin((p)*mat2(13.0,17.0,19.0,23.0))*1024.0)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 first=fragCoord,offset=vec2(0.25),cell=floor(first);
    vec2 value=HASH100(cell+offset+0.5);
    fragColor=vec4(value,0.0,1.0);
}
