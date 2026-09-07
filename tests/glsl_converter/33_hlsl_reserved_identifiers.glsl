float line(vec2 a, vec2 b){ return length(a-b); }
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 point = fragCoord/iResolution.xy;
    vec3 vector = vec3(point,1.0);
    float pass = line(point,vec2(0.5));
    vec3 texture = vector * pass;
    fragColor = vec4(texture,1.0);
}
