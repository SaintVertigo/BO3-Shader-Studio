#define vec1 float
struct W11_78 { vec1 a; vec1 b; };
struct W12_78 { vec1 a; vec2 b; };
struct W13_78 { vec1 a; vec3 b; };

vec1 su78(vec1 a, vec1 b) { return a - b; }
W11_78 su78(W11_78 a, vec1 b) { return W11_78(su78(a.a,b), a.b); }
W11_78 su78(vec1 a, W11_78 b) { return W11_78(su78(a,b.a), -b.b); }
W12_78 su78(W12_78 a, vec1 b) { return W12_78(su78(a.a,b), a.b); }
W12_78 su78(vec1 a, W12_78 b) { return W12_78(su78(a,b.a), -b.b); }
W13_78 su78(W13_78 a, vec1 b) { return W13_78(su78(a.a,b), a.b); }
W13_78 su78(vec1 a, W13_78 b) { return W13_78(su78(a,b.a), -b.b); }

W11_78 ab78(W11_78 p) { return W11_78(abs(p.a), p.b); }
W12_78 ab78(W12_78 p) { return W12_78(abs(p.a), p.b); }
W13_78 ab78(W13_78 p) { return W13_78(abs(p.a), p.b); }

W11_78 suab78(W11_78 p, vec1 s) { return su78(ab78(p), s); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    W11_78 p = W11_78(fragCoord.x * 0.01 - 1.0, 0.25);
    W11_78 r = suab78(p, 0.5);
    fragColor = vec4(r.a, r.b, 0.0, 1.0);
}
