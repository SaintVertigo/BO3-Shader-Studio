void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec3 base, extremity, texture;
    base=vec3(0.1); extremity=vec3(0.2); texture=vec3(0.3);
    fragColor=vec4(base+extremity+texture,1.0);
}
