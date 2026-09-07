struct Camera { mat3 rotate; vec3 pos; };
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    Camera camera;
    camera.rotate = mat3(1.0);
    vec3 v = normalize(vec3(fragCoord/iResolution.xy,1.0));
    vec3 a = camera.rotate * v;
    vec3 b = 0.3 * camera.rotate * v;
    fragColor = vec4(a+b,1.0);
}
