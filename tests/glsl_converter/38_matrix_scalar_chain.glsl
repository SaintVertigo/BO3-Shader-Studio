mat3 ident(){ return mat3(1.0); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    mat3 m=ident();
    vec3 p=vec3(fragCoord/iResolution.xy,1.0);
    vec3 a=m*0.3*p;
    vec3 b=p*0.5*m;
    fragColor=vec4(a+b,1.0);
}
