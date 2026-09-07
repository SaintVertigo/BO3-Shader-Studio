#define rot(a) mat2(cos(a),-sin(a),sin(a),cos(a))
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 p=fragCoord/iResolution.xy;
    p*=rot(iTime);
    p=p*rot(0.25);
    fragColor=vec4(p,0.0,1.0);
}
