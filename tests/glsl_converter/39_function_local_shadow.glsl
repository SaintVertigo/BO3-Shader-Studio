float arg(vec2 z){ return atan(z.y,z.x); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 z=fragCoord/iResolution.xy;
    float arg=arg(z);
    fragColor=vec4(vec3(arg),1.0);
}
