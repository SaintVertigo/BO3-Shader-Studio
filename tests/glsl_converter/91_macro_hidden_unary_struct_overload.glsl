// BO3_EXPECT_NOTE_CONTAINS: struct-valued GLSL macro invocation(s)
// BO3_EXPECT_NOTE_CONTAINS: exact unary custom-struct overload call(s)
#define vec1 float
#define NEGATE_91(x) ne91(x)

struct A91 { vec1 a; vec1 b; };
struct B91 { vec1 a; vec1 b; };

A91 ne91(A91 a) { return A91(-a.a, -a.b); }
B91 ne91(B91 a) { return B91(-a.a, -a.b); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A91 value = A91(fragCoord.x, fragCoord.y);
    value = NEGATE_91(value);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
