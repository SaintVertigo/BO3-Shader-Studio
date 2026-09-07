#define vec1 float
struct w11 { vec1 a; vec1 b; };
struct w12 { vec1 a; vec2 b; };
struct w13 { vec1 a; vec3 b; };
struct w14 { vec1 a; vec4 b; };

vec1 mu(vec1 a, vec1 b) { return a * b; }
vec2 mu(vec2 a, vec1 b) { return a * b; }
vec2 mu(vec1 a, vec2 b) { return b * a; }
vec3 mu(vec3 a, vec1 b) { return a * b; }
vec3 mu(vec1 a, vec3 b) { return b * a; }
vec4 mu(vec4 a, vec1 b) { return a * b; }
vec4 mu(vec1 a, vec4 b) { return b * a; }

w11 mu(w11 a, vec1 b) { return w11(mu(a.a, b), mu(a.b, b)); }
w11 mu(vec1 a, w11 b) { return mu(b, a); }
w12 mu(w12 a, vec1 b) { return w12(mu(a.a, b), mu(a.b, b)); }
w12 mu(vec1 a, w12 b) { return mu(b, a); }
w13 mu(w13 a, vec1 b) { return w13(mu(a.a, b), mu(a.b, b)); }
w13 mu(vec1 a, w13 b) { return mu(b, a); }
w14 mu(w14 a, vec1 b) { return w14(mu(a.a, b), mu(a.b, b)); }
w14 mu(vec1 a, w14 b) { return mu(b, a); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    w11 x = mu(2.0, w11(3.0, 4.0));
    fragColor = vec4(x.a, x.b, 0.0, 1.0);
}
