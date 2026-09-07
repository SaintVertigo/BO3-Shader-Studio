void mainImage(out vec4 fragColor, in vec2 fragCoord){
    mat2 m=mat2(2.0,0.0,0.0,4.0);
    vec2 p=inverse(m)*(fragCoord/iResolution.xy);
    fragColor=vec4(p,0.0,1.0);
}
