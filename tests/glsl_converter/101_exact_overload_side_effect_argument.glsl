// BO3_EXPECT_NOTE_CONTAINS: Preserved function-call evaluation
// BO3_EXPECT_CONVERTED_CONTAINS: __bo3_exact_call_
// BO3_EXPECT_CONVERTED_COUNT: bump101( => 2

struct A101 { float x; float y; };
struct B101 { float x; float y; };

float counter101 = 0.0;

float bump101(float value)
{
    counter101 += 1.0;
    return value;
}

A101 combine101(A101 value, float scale)
{
    return A101(value.x * scale, value.y * scale);
}

B101 combine101(B101 value, float scale)
{
    return B101(value.x * scale, value.y * scale);
}

A101 duplicate101(A101 value)
{
    return A101(value.x + value.x, value.y + value.y);
}

B101 duplicate101(B101 value)
{
    return B101(value.x + value.x, value.y + value.y);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A101 value = A101(fragCoord.x, fragCoord.y);
    A101 result = duplicate101(combine101(value, bump101(2.0)));
    fragColor = vec4(result.x, result.y, counter101, 1.0);
}
