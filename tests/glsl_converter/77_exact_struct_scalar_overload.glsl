#define vec1 float
struct W11_77 { vec1 a; vec1 b; };
struct W12_77 { vec1 a; vec2 b; };
struct W13_77 { vec1 a; vec3 b; };
struct Outer77 { W13_77 x; W13_77 y; W13_77 z; };
vec1 mu77(vec1 a,vec1 b){return a*b;}
W11_77 mu77(W11_77 a,vec1 b){return W11_77(mu77(a.a,b),mu77(a.b,b));}
W11_77 mu77(vec1 a,W11_77 b){return mu77(b,a);}
W12_77 mu77(W12_77 a,vec1 b){return W12_77(mu77(a.a,b),mu77(a.b,b));}
W12_77 mu77(vec1 a,W12_77 b){return mu77(b,a);}
W13_77 mu77(W13_77 a,vec1 b){return W13_77(mu77(a.a,b),mu77(a.b,b));}
W13_77 mu77(vec1 a,W13_77 b){return mu77(b,a);}
W13_77 mu77(W13_77 a,W13_77 b){return W13_77(mu77(a.a,b.a),a.b*b.a+a.a*b.b);}
Outer77 mu77(Outer77 p,vec3 s){return Outer77(mu77(p.x,s.x),mu77(p.y,s.y),mu77(p.z,s.z));}
void mainImage(out vec4 fragColor,in vec2 fragCoord){
    Outer77 p=Outer77(W13_77(.2,vec3(.1)),W13_77(.3,vec3(.2)),W13_77(.4,vec3(.3)));
    Outer77 r=mu77(p,vec3(2.0,3.0,4.0));
    fragColor=vec4(r.x.a,r.y.a,r.z.a,1.0);
}
