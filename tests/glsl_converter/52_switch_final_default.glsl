float choose(int i){
    float x=0.0;
    switch(i){ case 0: x=1.0; break; default: x=2.0; }
    return x;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord){ fragColor=vec4(vec3(choose(int(fragCoord.x)%2)),1.0); }
