// BO3_EXPECT_NOTE_CONTAINS: macro-safe explicit matrix multiplication
// BO3_EXPECT_CONVERTED_CONTAINS: mul(float2x2(
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: (p)*float2x2

#define HASH98(p) fract(sin((p)*mat2(127.1,311.7,269.5,183.3))*43758.5453123)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 value=HASH98(fragCoord);
    fragColor=vec4(value,0.0,1.0);
}
