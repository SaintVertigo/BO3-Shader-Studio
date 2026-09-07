vec4 sampleTex(sampler2D s, vec2 uv){ return texture(s,uv); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    fragColor=sampleTex(iChannel0,fragCoord/iResolution.xy);
}
