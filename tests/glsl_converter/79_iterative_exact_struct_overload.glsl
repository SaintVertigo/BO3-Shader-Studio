#define vec1 float

struct W11_79 { vec1 a; vec1 b; };
struct W13_79 { vec1 a; vec3 b; };
struct DAm2_79 { W13_79 x; W13_79 y; W13_79 z; };

vec1 su79(vec1 a, vec1 b) { return a - b; }
W11_79 su79(W11_79 a, vec1 b) { return W11_79(su79(a.a,b), a.b); }
W11_79 su79(vec1 a, W11_79 b) { return W11_79(su79(a,b.a), -b.b); }
W13_79 su79(W13_79 a, vec1 b) { return W13_79(su79(a.a,b), a.b); }
W13_79 su79(vec1 a, W13_79 b) { return W13_79(su79(a,b.a), -b.b); }

W11_79 ab79(W11_79 p) { return W11_79(abs(p.a), p.b); }
W13_79 ab79(W13_79 p) { return W13_79(abs(p.a), p.b); }

W13_79 suab79(W13_79 p, vec1 s) { return su79(ab79(p), s); }
DAm2_79 suab79(DAm2_79 p, vec3 s)
{
    return DAm2_79(suab79(p.x,s.x), suab79(p.y,s.y), suab79(p.z,s.z));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    W13_79 q = W13_79(fragCoord.x * 0.01 - 1.0, vec3(0.25, 0.5, 0.75));
    DAm2_79 p = DAm2_79(q, q, q);
    DAm2_79 r = suab79(p, vec3(0.1, 0.2, 0.3));
    fragColor = vec4(r.x.a, r.y.a, r.z.a, 1.0);
}
