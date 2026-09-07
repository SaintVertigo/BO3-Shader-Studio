void mainImage(out vec4 fragColor, in vec2 fragCoord){
    ivec2 s=textureSize(iChannel0,0);
    vec2 uv=fragCoord/vec2(s);
    fragColor=texture(iChannel0,uv);
}
