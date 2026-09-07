#define M(T)mat2(cos(T+length(o)+vec4(0,11,33,0)))
void main(){vec3 d=vec3((r*.5-gl_FragCoord.xy)/r.y,1.)*.3,p,o;p.z-=6.;for(int i=0;i<64;i++)o=p,o.zx*=M(t),o.zy*=M(t*.2),p+=(length(vec2(fract(length(o.xy))-.5,o.z))-.01)*d;gl_FragColor=(vec4(1,.2,2,0))/length(p);}
