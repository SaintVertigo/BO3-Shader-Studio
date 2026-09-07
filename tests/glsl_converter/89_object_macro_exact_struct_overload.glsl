// BO3_EXPECT_NOTE_CONTAINS: object-like GLSL macro value type(s)
// BO3_EXPECT_NOTE_CONTAINS: function-body exact custom-struct overload call(s)
#define vec1 float
#define HALF_TURN_89 acos(-1.0)

struct A89 { vec1 a; vec1 b; };
struct B89 { vec1 a; vec1 b; };

vec1 mu89(vec1 a, vec1 b) { return a * b; }
vec1 di89(vec1 a, vec1 b) { return a / b; }
A89 di89(A89 a, vec1 b) { return A89(di89(a.a,b), di89(a.b,b)); }
B89 di89(B89 a, vec1 b) { return B89(di89(a.a,b), di89(a.b,b)); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A89 value = A89(fragCoord.x, fragCoord.y);
    vec1 divisor = di89(fragCoord.x + 1.0, mu89(HALF_TURN_89, 0.5));
    value = di89(value, divisor);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
