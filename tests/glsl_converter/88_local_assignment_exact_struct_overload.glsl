// BO3_EXPECT_NOTE_CONTAINS: function-body exact custom-struct overload call(s)
#define vec1 float

struct A88 { vec1 a; vec1 b; };
struct B88 { vec1 a; vec1 b; };

vec1 di88(vec1 a, vec1 b) { return a / b; }
A88 di88(A88 a, vec1 b) { return A88(di88(a.a,b), di88(a.b,b)); }
B88 di88(B88 a, vec1 b) { return B88(di88(a.a,b), di88(a.b,b)); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A88 value = A88(fragCoord.x, fragCoord.y);
    vec1 scale = 2.0;
    value = di88(value, scale);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
