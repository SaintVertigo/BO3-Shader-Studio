// BO3_EXPECT_NOTE_CONTAINS: transitive GLSL macro wrapper(s)
// BO3_EXPECT_NOTE_CONTAINS: struct-valued GLSL macro invocation(s)
#define vec1 float
#define DIRECT_94(x) ma94(x, 0.0)
#define WRAPPED_94(x) DIRECT_94(x)

struct A94 { vec1 a; vec1 b; };
struct B94 { vec1 a; vec1 b; };

A94 ma94(A94 a, vec1 b) { return A94(max(a.a,b), max(a.b,b)); }
B94 ma94(B94 a, vec1 b) { return B94(max(a.a,b), max(a.b,b)); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A94 value = A94(fragCoord.x, fragCoord.y);
    value = WRAPPED_94(value);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
