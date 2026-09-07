const float M_PI=3.14159265;
const int numLights=2;
mat2 viewMatrix(float a){ return mat2(cos(a),-sin(a),sin(a),cos(a)); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 p=(fragCoord/iResolution.xy)*viewMatrix(M_PI/float(numLights));
    fragColor=vec4(p,0.0,1.0);
}
