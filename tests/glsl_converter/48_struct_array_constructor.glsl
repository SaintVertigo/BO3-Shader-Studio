struct Seg { vec2 a; vec2 b; };
const int N=2;
const Seg segs[N] = Seg[N](Seg(vec2(0.0),vec2(1.0)), Seg(vec2(1.0),vec2(0.0)));
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    Seg local[2] = Seg[](Seg(vec2(0.1),vec2(0.2)), Seg(vec2(0.3),vec2(0.4)));
    fragColor = vec4(segs[0].a + local[1].b,0.0,1.0);
}
