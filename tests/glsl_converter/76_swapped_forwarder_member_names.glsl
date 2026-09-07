#define vec1 float
struct A76 { vec1 x; };
struct B76 { vec2 x; };
struct W76 { A76 a; B76 b; };
A76 scaleA76(A76 v, vec1 s){ return A76(v.x*s); }
B76 scaleB76(B76 v, vec1 s){ return B76(v.x*s); }
W76 op76(W76 a, vec1 b){ return W76(scaleA76(a.a,b),scaleB76(a.b,b)); }
W76 op76(vec1 a, W76 b){ return op76(b,a); }
void mainImage(out vec4 fragColor,in vec2 fragCoord){
    W76 w=W76(A76(.25),B76(vec2(.5,.75)));
    W76 r=op76(2.0,w);
    fragColor=vec4(r.a.x,r.b.x,1.0);
}
