// BO3_EXPECT_NOTE_CONTAINS: exact unary custom-struct overload call(s)
#define vec1 float

struct A90 { vec1 a; vec1 b; };
struct B90 { vec1 a; vec1 b; };

A90 ne90(A90 a) { return A90(-a.a, -a.b); }
B90 ne90(B90 a) { return B90(-a.a, -a.b); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A90 value = A90(fragCoord.x, fragCoord.y);
    value = ne90(value);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
