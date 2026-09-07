struct Ray { vec3 o; vec3 d; bool h; vec3 hp; float m; };
Ray makeRay(){ Ray r; r.o=vec3(0.0); r.d=vec3(0.0,0.0,1.0); r.h=false; return r; }
void cast(inout Ray r){ if(r.d.z>0.0){ r.h=true; r.hp=r.o+r.d; r.m=1.0; } }
void mainImage(out vec4 fragColor, in vec2 fragCoord){ Ray r=makeRay(); cast(r); fragColor=vec4(r.hp,r.h?1.0:0.0); }
