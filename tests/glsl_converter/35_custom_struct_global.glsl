struct State { float x; vec3 c; };
State gState;
void updateState(float t){ gState.x=t; gState.c=vec3(t); }
void mainImage(out vec4 fragColor, in vec2 fragCoord){
    updateState(iTime);
    fragColor=vec4(gState.c,1.0);
}
