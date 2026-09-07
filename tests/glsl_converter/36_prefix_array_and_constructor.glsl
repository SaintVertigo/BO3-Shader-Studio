const int N=3;
float sumPts(vec2[N] p){ return p[0].x+p[1].x+p[2].x; }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2[N] p = vec2[N](vec2(0.1),vec2(0.2),vec2(0.3));
    fragColor=vec4(vec3(sumPts(p)),1.0);
}
