#define RETZERO() return vec3(0.25);
vec3 f(){ RETZERO(); }
void mainImage(out vec4 fragColor,in vec2 fragCoord){
    fragColor=vec4(f(),1.0);
}
