// BO3_EXPECT_NOTE_CONTAINS: exact custom-struct overload call(s)
#define vec1 float

struct A84 { vec1 a; vec1 b; };
struct B84 { vec1 a; vec1 b; };

A84 c84(vec1 a, vec1 b) { return A84(a,b); }
A84 c84(vec1 a) { return A84(a,a); }
B84 d84(vec1 a, vec1 b) { return B84(a,b); }

vec1 su84(vec1 a, vec1 b) { return a-b; }
A84 su84(A84 a, A84 b) { return A84(su84(a.a,b.a), su84(a.b,b.b)); }
B84 su84(B84 a, B84 b) { return B84(su84(a.a,b.a), su84(a.b,b.b)); }

A84 fr84(A84 a) { return A84(fract(a.a), floor(a.b)); }

A84 pmod84(A84 a)
{
    // The exact su84(A84,A84) overload can only be selected by the converter
    // after proving that a.a/a.b+.5 is scalar, then following
    // c84(float)->A84 and fr84(A84)->A84.
    return su84(fr84(c84(a.a/a.b+.5)), c84(.5,.0));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A84 p = pmod84(c84(fragCoord.x * .01 + .25, 2.0));
    fragColor = vec4(p.a, p.b, 0.0, 1.0);
}
