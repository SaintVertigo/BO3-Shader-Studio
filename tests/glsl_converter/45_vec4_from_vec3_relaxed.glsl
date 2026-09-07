void mainImage(out vec4 fragColor, in vec2 fragCoord){
    float d=fragCoord.x/iResolution.x;
    vec4 c=vec4(vec3(d));
    fragColor=c;
}
