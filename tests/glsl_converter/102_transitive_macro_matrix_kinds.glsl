// BO3_EXPECT_NOTE_CONTAINS: call-site matrix macro
// BO3_EXPECT_CONVERTED_CONTAINS: #define INNER102(p) ((p)*float2x2
// BO3_EXPECT_CONVERTED_CONTAINS: #define OUTER102(p) INNER102(p)
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: INNER102(fragCoord)

#define INNER102(p) ((p)*mat2(1.0, 2.0, 3.0, 4.0))
#define OUTER102(p) INNER102(p)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 direct = INNER102(fragCoord);
    mat2 scaled = OUTER102(2.0);
    vec2 indirect = scaled * vec2(0.25, 0.75);
    fragColor = vec4(direct + indirect, 0.0, 1.0);
}
