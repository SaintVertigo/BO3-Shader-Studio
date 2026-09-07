// BO3_EXPECT_NOTE_CONTAINS: exact custom-struct overload call(s)
#define vec1 float

struct V11_83 { vec1 a; vec1 b; };
struct V22_83 { vec2 a; vec2 b; };

V11_83 c11_83(vec1 a, vec1 b) { return V11_83(a,b); }
V11_83 c11_83(vec1 a) { return V11_83(a,a); }
V22_83 c22_83(vec2 a, vec2 b) { return V22_83(a,b); }
V22_83 c22_83(vec2 a) { return V22_83(a,a); }

vec1 su83(vec1 a, vec1 b) { return a-b; }
V11_83 su83(V11_83 a, V11_83 b) { return V11_83(su83(a.a,b.a), su83(a.b,b.b)); }
V22_83 su83(V22_83 a, V22_83 b) { return V22_83(su83(a.a,b.a), su83(a.b,b.b)); }

V11_83 frfl83(V11_83 a) { return V11_83(fract(a.a), floor(a.b)); }
V22_83 frfl83(V22_83 a) { return V22_83(fract(a.a), floor(a.b)); }

V11_83 mu83(V11_83 a, V11_83 b) { return V11_83(a.a*b.a, a.b*b.b); }
V22_83 mu83(V22_83 a, V22_83 b) { return V22_83(a.a*b.a, a.b*b.b); }

V11_83 pmod83(V11_83 a)
{
    return mu83(su83(frfl83(c11_83(a.a/a.b+.5)), c11_83(.5,.0)), c11_83(a.b,1.));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    V11_83 p = pmod83(c11_83(fragCoord.x * .01 + .25, 2.0));
    fragColor = vec4(p.a, p.b, 0.0, 1.0);
}
