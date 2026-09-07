void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec3 col;
    float f;
    for(int i=0;i<3;i++) col[i]=float(i)*0.1;
    f += col.x;
    fragColor=vec4(col+f,1.0);
}
