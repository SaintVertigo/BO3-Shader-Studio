#define rot(a) mat2(cos(a),sin(a),-sin(a),cos(a))
float orbit;
float map(vec3 p){
	p.yz*=rot(t*0.1+2.);
  p.xz*=rot(t*0.2+2.);
	p=abs(p)-2.5;
	if(p.x<p.z)p.xz=p.zx;
	if(p.y<p.z)p.yz=p.zy;
 	if(p.x<p.y)p.xy=p.yx;
 	float s=2.;
	vec3  p0=p*1.3;
	for (float i=0.;i<5.;i++){
		  p=1.-abs(p-1.);
  		float k=-5.*clamp(.6*max(1.5/dot(p,p),.8),0.,1.);
    	s*=abs(k);
   		p*=k;
      p+=p0;
    	p.yz*=rot(4.6);
    }
	orbit = log2(s);
	float a=5.;
	p.yz-=clamp(p.yz,-a,a);
	return length(p.xy)/s;
}

void main(){
  vec2 uv=(2.*gl_FragCoord.xy-r)/r.y;
  vec3 ro=vec3(0,0,-18);
  vec3 rd=normalize(vec3(uv,3));
  float h=0.,d,i;
	for(i=1.;i<100.;i++){
    d=map(ro+rd*h);
  	if(d<.0001)break;
    h+=d;
	}
  o.xyz+=5.*(cos(vec3(4,13,7)+orbit*1.)*.5+.5)/i;
  o.xyz+=20./i;
}
