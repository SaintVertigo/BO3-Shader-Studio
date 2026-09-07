struct Ray { vec3 o; vec3 d; };
Ray makeRay(vec3 o, vec3 d) { return Ray(o,d); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    Ray r = Ray(vec3(fragCoord/iResolution.xy,0.0), vec3(0.0,0.0,1.0));
    fragColor = vec4(makeRay(r.o,r.d).o,1.0);
}
