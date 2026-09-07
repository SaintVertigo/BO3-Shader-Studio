struct A { vec2 p; float w; };
A makeA(vec2 p){ return A(p,1.0); }
struct B { vec3 q; };
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    A a = makeA(fragCoord/iResolution.xy);
    fragColor = vec4(a.p,a.w,1.0);
}
