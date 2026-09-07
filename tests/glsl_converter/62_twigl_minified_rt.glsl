#define s(a)cos((vec3(0.,.6,-.6)+a)*3.)*.5+.5
void main(){vec2 p=(gl_FragCoord.xy*2.-r)/r;p=vec2(dot(p,p));vec3 c,h;float d;for(float i=1.;i<50.;i++)d=t*i*.02,h=(1.+sin(t*i*.13)*.5)*s(i*d/.7),c+=.002/abs(p.y+sin(p.x+d+i*.3)*.75)*h;gl_FragColor=vec4(c,1);}
