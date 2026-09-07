float h(const float n){ return fract(sin(n)*1234.5); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    fragColor = vec4(vec3(h(fragCoord.x)),1.0);
}
