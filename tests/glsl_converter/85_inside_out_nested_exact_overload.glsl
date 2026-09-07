// BO3_EXPECT_NOTE_CONTAINS: inside-out normalization
#define vec1 float

struct V85 { vec1 a; vec1 b; };
struct W85 { vec1 a; vec1 b; };

V85 c85(vec1 a, vec1 b) { return V85(a,b); }
V85 c85(vec1 a) { return V85(a,a); }
W85 d85(vec1 a, vec1 b) { return W85(a,b); }

vec1 su85(vec1 a, vec1 b) { return a-b; }
V85 su85(V85 a, V85 b) { return V85(su85(a.a,b.a), su85(a.b,b.b)); }
W85 su85(W85 a, W85 b) { return W85(su85(a.a,b.a), su85(a.b,b.b)); }

V85 fr85(V85 a) { return V85(fract(a.a), floor(a.b)); }

V85 mu85(V85 a, V85 b) { return V85(a.a*b.a, a.b*b.b); }
W85 mu85(W85 a, W85 b) { return W85(a.a*b.a, a.b*b.b); }

V85 p85(V85 a)
{
    // Both the outer mu85(...) and the nested su85(...) are exact custom-struct
    // overloads. The converter must lower the nested su85 first; expanding mu85
    // first would bury su85 beneath generated member accesses.
    return mu85(su85(fr85(c85(a.a/a.b+.5)), c85(.5,.0)), c85(a.b,1.));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    V85 p = p85(c85(fragCoord.x * .01 + .25, 2.0));
    fragColor = vec4(p.a, p.b, 0.0, 1.0);
}
