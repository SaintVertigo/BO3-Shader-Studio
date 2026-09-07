float maybeValue(float x){ if(x>0.0) return x; }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    fragColor=vec4(vec3(maybeValue(fragCoord.x)),1.0);
}
