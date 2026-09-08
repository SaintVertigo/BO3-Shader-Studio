// BO3_FAST_REGRESSION_DEFER_CASE: oversized macro stress fixture; automatic Tester CI defers full conversion + FXC to full/manual CI
// BO3_FAST_REGRESSION_SKIP_FXC: oversized macro stress fixture; full/manual CI still performs FXC O3 validation

vec4 iMouseZwFix(vec4 m,bool NewCoke
 ){if(m.z>0.){ //while mouse down
    if(m.w>0.)return m;//mouse was clicked in THIS     iFrame 
    else m.w=-m.w      //mosue was clicked in previous iFrame
    //remember, MouseDrag advances the iFrame Count, even while paused !!
 ;}else{if(!NewCoke||m.w>0.)return m.xyxy; //OPTIONAL onMouseUp (fold or whatever)
    m.zw=-m.zw;}
  return m;}

//alphacompositing fixed
//lacks root-solving shapes (parabola-ellipse)
//canvas "TinyBri300OHGDAPo14"
//lots of code from [inogo quilez] [mercury] [dr2] [David Hoskins]
//has iFrame fixed
//has unified smin() smooth-Boolean-fuzzy-logic variants
//has complex 2d transforms (possibly buggy,checked as much as i care and know this stuff)
//has some rgba-colorspace-gradients/indicators (but surely not all that i care for,cieluelch is missing)
//has noise (upper ones are better for (hyperplanes of) less domains
//- tri,which is food for very fast volumetric storms
//- h41=[fastest fbm with normals],that is just fast fbm noise with (poorly estimated) 3d normals
//- hfd=[hash(fract(dot()))],that does not use sin(fract(a)),by David Hopkins
//- cel=cellular noise,parallelized/fast vorley noise with 2 shortest distances in 2d or 3d,with hash as additional static domain.
//- s13=[simplex],highest quality/performance noise,generally too slow for 3d spheretracking (as displacement or heightmap)
//- - lacks bayerMatrix,blueHash<-part of higher-domain content.
//has DA derivative arithmetic
//- has issues with Param2:
//- - decapitated filling lists with zeroes,seems VERY nonsensical
//- - DA mixing differs from MAT mixin,not just in structure.
//- - ... previous DA shaders made the error od declaring DA-mixing as MAT-mixind with DA-parameters?
//- - ... or was that just cases where MAT is identical to DA?
//has alias-folds fixed,with mercury.sexy/hg_sdf
//- are declared as defines,for
//- - N-dimensional inputs,where that function still makes sense in higher dimensions
//- - uses the same labels as DA,so depending on input it can also calculate derivatives
//- Includes minified HG_SDF by Mercury mercury.sexy/hg_sdf
//- omitted platonic solids because the original code uses arrays
//- Some functions are converted to macros because they would be even smaller when preprocessed.
//- Another macro,_M,is really filling up almost identical code as to make the result even smaller,

//has AD=automatic Differentiation via chain rule but only demoes it in 1d
//note to self,default background is black,otherwise tiny shadertoy shaders will be white image on white background.

#define vec1 float
#define norma normalize
//#define ss smoothstep
#define pi acos(-1.)
#define tau pi*2.
//float tau=6.2831853071795864769252867665590057683943387987502116419;//trig definition can be better for smarter compilers.
#define cl(a,b,c) mi(ma(a,0.),1.)//till i define this for AD
#define sat(x) cl(x,0.,1.)
#define dd(a)dot(a,a)
#define u5(a)((a)*.5+.5)
#define u2(a)((a)*2.-1.)
#define u5cos(a) u5(cos(a))
struct v11{vec1 a;vec1 b;};
struct v22{vec2 a;vec2 b;};
struct v33{vec3 a;vec3 b;};//for 2 domains(density/distance field)
struct v44{vec4 a;vec4 b;};
struct v444{vec4 a;vec4 b;vec4 c;};//for 3 domains(density/distance field); also used by DA,but as bridge to non-DA.
struct v333{vec3 a;vec3 b;vec3 c;};
struct v3333{vec3 a;vec3 b;vec3 c;vec3 d;};
struct v222{vec2 a;vec2 b;vec2 c;};
//v222 g222(vec3 a,vec3 b){return v222(vec2(a.x,b.x),vec2(a.y,b.y),vec2(a.z,b.z));}
v222 g222(v33 a){return v222(vec2(a.a.x,a.b.x),vec2(a.a.y,a.b.y),vec2(a.a.z,a.b.z));}
v3333 muv(v3333 a,vec4 b){return v3333(a.a*b.x,a.b*b.y,a.c*b.z,a.d*b.w);}
vec4 ddv(v3333 a){return vec4(dd(a.a),dd(a.b),dd(a.c),dd(a.d));}
//above [v*] struct implies matrix arithmetic,below [w*] struct implies AD,wherelater spaces are (analytic) lower-exponent-differentials
//[.b] is always a derivative of [.a],along one (of multiple) domain(s)
struct w11{vec1 a;vec1 b;};
struct w12{vec1 a;vec2 b;};
struct w13{vec1 a;vec3 b;};
struct w14{vec1 a;vec4 b;};
struct DAm2{w13 x;w13 y;w13 z;};//for 3 domains (density/distance field) 
struct DAm1{w12 x;w12 y;};//for 2 domains (heightmap,isoline,contour)
struct DAm0{w11 x;};//for 2 domains (heightmap,isoline,contour)
#define eul 2.7182818284590452353602874713526624977572470936999595749
//eul=exp(1.)???
//"goldenRatio"=phi+1=Phi :where: 1/phi=phi-1&&1/Phi=Phi-1
//https://en.wikipedia.org/wiki/Golden_ratio
#define phi (sqrt(5.)*.5-.5)
//goldenRatio is great fun with fract(),good for hashes.
#define Phi (sqrt(5.)*.5+.5)








w11 ma(w11 a,vec1 b){return w11(max(a.a,b),max(a.b,b));}
w11 mi(w11 a,vec1 b){return w11(min(a.a,b),min(a.b,b));}
//#define ss(a,u)smoothstep(a,-a,u)//bad namespace for this,more trouble than its worth
vec1 suv(vec4 a){return dot(vec4(1),a);}vec1 suv(vec3 a){return dot(vec3(1),a);}vec1 suv(vec2 a){return a.x+a.y;}//sum of vector
#define minx(a,b)mix(b,a,step(a.x,b.x))
#define manx(a,b)mix(a,-b,step(a.x,-b.x))
#define maxx(a,b)-minx(-a,-b)


//2d zoom
#define ViewZoom 3.
//divide by/aa for hairline drawing and sharp smoothstep()
#define Aa (min(iResolution.x,iResolution.y)/ViewZoom)
#define fra(u)(u-.5*iResolution.xy)*ViewZoom/iResolution.y//usually first function of mainImage(),not typecast.
//DAm2 maxdm(DAm2 a,v2 p){return DAm2(maxd(a.a,p.x),maxd(a.y,p.y),maxd(a.z,p.z));}
//DAm2 mindm(DAm2 a,v2 p){return DAm2(mind(a.x,p.x),mind(a.y,p.y),mind(a.z,p.z));}
//DAm2 maxdm(DAm2 a,vec1 p){return maxdm(a,v2(p));}
//DAm2 mindm(DAm2 a,vec1 p){return mindm(a,v2(p));}//needed for fast clamping
//w13 maxdm(w13 a,w13 b,w13 c){return maxd(maxd(a,b),c);}
//w13 maxdm(DAm2 a){return maxdm(a.a,a.y,a.z);}
//The 3(or 2)dimensions|domains 
//...are resolved with [struct DAmN{}] and [da_domain(vN p)]:
//where N=number of domains,range [0..3]
v444 da_domain(vec3 p){return v444(vec4(p.x,1,0,0),vec4(p.y,0,1,0),vec4(p.z,0,0,1));}
v33 da_domain(vec2 p){return v33(vec3(p.x,1,0),vec3(p.y,0,1));}
vec2 da_domain(vec1 p){return vec2(p,1);}//for 1 domain(linear equation)
//vNN() declares c11() c22() c33() c44()... for implicit typecasting into structs; v11 v22 v33 v44
v11 c11(vec1 a,vec1 b){return v11(a,b);}
v11 c11(vec1 a){return v11(a,a);}
#define vNNv(z,x) {return z(x(a),x(b));}
#define vNNu(z,x,y) z x(vec1 a,y b)vNNv(z,y) z x(y a,vec1 b)vNNv(z,y) z x(vec1 a,vec1 b)vNNv(z,y)
#define vNN(z,y,x) vNNu(z,y,x) z y(x a,x b){return z(a,b);} z y(x a){return z(a,a);}
vNN(v22,c22,vec2)
vNN(v33,c33,vec3)
vNN(v44,c44,vec4)


//param1MAT dreaming of vec5 and vec2x2,also,this keeps define-namespace unused,by declating function sets!
//lets me type " a=min(vec3(a),float(b)) a=pow(vec3(a),float(b)) " with less explicit typecasting and shorter aliases:
//negate,reciprocal,abs,log,sin,asin,cos,acos,tan,atan,fract,floor
#define NE(y,z) z ne(z a){return -a;}y ne(y a){return y(-a.a,-a.b);}
NE(v11,vec1)NE(v22,vec2)NE(v33,vec3)NE(v44,vec4)
w13 ne(w13 a){return w13(-a.a,-a.b);}
w12 ne(w12 a){return w12(-a.a,-a.b);}
w11 ne(w11 a){return w11(-a.a,-a.b);}
#define def1E(z,y,x) x z(x a){return y(a);}
#define def1(a,b) def1E(a,b,vec1) def1E(a,b,vec2) def1E(a,b,vec3) def1E(a,b,vec4)
#define re(a) di(1.,b)
def1(ab,abs)def1(sg,sign)def1(sq,sqrt)
def1(ln,log)def1(ex,exp)
def1(si,sin)def1(asi,asin)
def1(co,cos)def1(aco,acos)
def1(ta,tan)def1(ata,atan)
def1(fr,fract)
def1(fl,floor)
//----param2MAT
#define mi0(a) mi(a,0.)
#define ma0(a) ne(mi0(ne(a)))
#define def2U(z,y,x) x z(vec1 a,x b){return y(x(a),b);} x z(x a,vec1 b){return y(a,x(b));}
#define def2E(z,y,x) x z(x a,x b){return y(a,b);}
#define def2(a,b) def2E(a,b,vec1) def2E(a,b,vec2) def2E(a,b,vec3) def2E(a,b,vec4)
#define defU(a,b) def2(a,b) def2U(a,b,vec2) def2U(a,b,vec3) def2U(a,b,vec4)
#define sub(a,b) ((a)-(b))
#define add(a,b) ((a)+(b))//sub(a,ne(b))
#define div(a,b) ((a)/(b))
#define mul(a,b) ((a)*(b)) //di(a,re(b)) reciprocal define may lose time&precision
//a lot of these just reserve namespaces //dot() is special for folding to a vec1 uses defD()
#define def2D(z,y,x) vec1 z(vec1 a,x b){return y(x(a),b);} vec1 z(x a,vec1 b){return y(a,x(b));}
#define def2F(z,y,x) vec1 z(x a,x b){return y(a,b);}
#define defF(a,b) def2F(a,b,vec1) def2F(a,b,vec2) def2F(a,b,vec3) def2F(a,b,vec4)
#define defD(a,b) defF(a,b) def2D(a,b,vec2) def2D(a,b,vec3) def2D(a,b,vec4)
//vNN operands foldlike this:
#define vXX(z,y) z(y(a.a,b),y(a.b,b))
#define vXY(z,y) z(y(a.a,b.a),y(a.b,b.a))
#define vvZ(z,w,v,y) w z(w a,v b){return y(w,z);}w z(w a,vec1 b){return y(w,z);}w z(v11 a,v b){return y(w,z);}
#define vvY(z,w,v,y) w z(w a,v b){return y(w,z);}w z(v a,w b){return y(w,z);}w z(w a,w b){return y(w,z);}
#define vvX(y,w,v) vvZ(y,w,v,vXX) vvY(y,w,v11,vXY)
#define vMM(z) vvX(z,v22,vec2)vvX(z,v33,vec3)vvX(z,v44,vec4)v11 z(v11 a,v11 b){return vXY(v11,z);}v11 z(v11 a,vec1 b){return vXX(v11,z);}
//alias2_NinputAsVec; due to symmetrically defined alising,in favor of min() max() functions (previously called miv() mav()):
//note that dt(vec4 a)==dot(dot(a.x,a.y),dot(a.z,a.w)),equivalent to ad(vec4 a)
//note that po(vec4 a)==pow(pow(a.x,a.y),pow(a.z,a.w)),which is quite silly
//note that su(vec4 a)==(a.x-a.y)-(a.z-a.w)=a.x+a.w-a.y-a.z=a.x+a.w-(a.y+a.z)
//note that di(vec4 a)==(a.x/a.y)/(a.z/a.w)=a.x*a.w/a.y/a.z=a.x*a.w-(a.y*a.z)
//alternatively fo() couls also be defined as dot(a,1.),where parts of a are negated or reciprocal
#define Fo1(u,t) vec1 u(t a){return u(a.x,a.y);}
#define Fo2(u,t) vec1 u(t a){return u(u(a.xy),a.z);}
#define Fo3(u,t) vec1 u(t a){return u(u(a.xy),u(a.zw));}
#define Fon(u) Fo1(u,vec2) Fo2(u,vec3) Fo3(u,vec4)
#define defM(a,b) defU(a,b) vMM(a) Fon(a)
defM(po,pow)
defM(mi,min)defM(ma,max)
defM(su,sub)defM(di,div)
defM(ad,add)defM(mu,mul)
defM(mo,mod)
defM(st,step)
//higher domains for high-domain-permutations/zOrder/Noise
//v33 su(vec3 a,v33 b){return v33(a-b.a,a-b.b);}
v33 g33(vec2 a){return v33(vec3(a.x),vec3(a.y));}
v333 su(vec3 a,v333 b){return v333(a-b.a,a-b.b,a-b.c);}
v333 g333(v33 a,vec3 b){return v333(a.a,a.b,b);}
v3333 g3333(vec3 a,v333 b){return v3333(a,b.a,b.b,b.c);}
v44 fl(v44 a){return v44(fl(a.a),fl(a.b));}
v44 fr(v44 a){return v44(fr(a.a),fr(a.b));}
//polar Complex dot()
const vec3 vs=vec3(-1,0,1);
#define le(a) sq(dd(a))
defD(dt,dot)
#define dd(a) dot(a,a)
//vMM(dt)//breaks a pattern,due to being a foldingprojection
Fon(dt)
vec2 perp(vec2 a){return a.yx*vs.xz;}
//http://mathworld.wolfram.com/PerpDotProduct.html
//http://wiki.secondlife.com/wiki/Geometric#Line_and_Line.2C_intersection_point
float perpdot(vec2 a,vec2 b){return dot(perp(a),b);}//==determinant(mat2(a,b)),aka cross2(),for also being the crossproduct()of a mat2.
//
vec2 cs(vec2 a){return vec2(cos(a.x),sin(a.y));}
vec2 cs(vec1 a){return cs(vec2(a,a));}
vec2 sub2(vec4 a){return a.xy-a.zw;}//substract in modulo 2
//one idea is to define all functions as vec4,and then expand to vec4,and then just ignore most domains later on.
//this should work in some contexts,but may not work in all contexts,this frame can make a library much smaller,but it also is an overhead.
//return vec4()and explicitly fill all unused domains with n
vec4 an(vec4 a,float n){return a;}
vec4 an(vec3 a,float n){return vec4(a,n);}
vec4 an(vec2 a,float n){return an(vec3(a,n),n);}
vec4 an(vec2 a,vec2 n){return vec4(a,a);}//for c2()and c4()this is mod2
vec4 an(float a,float n){return an(vec2(a,n),n);}
#define ana1(a)an(a,1.)
#define anaa(a)an(a,a)
//#define ana0(a)an(a,0)//leat worksafe
//#define anaa(a)an(a,a)//unpredictable worksafety
//exmaple below defines c2()as framed c4;
#define c4(a)((a)*vec4(1,-1,1,-1))
//c2(c)-(c.a-c.b*i)
#define c2(a)c4(anaa(a)).xy
mat2 r1(vec1 a){vec2 b=cs(a);return mat2(b.xy,-b.y,b.x);}
void pR(inout vec2 p,float a){p=co(a)*p+si(a)*vec2(p.y,-p.x);}//rotate point by a
void pR45(inout vec2 p){p=(p+vec2(p.y,-p.x))*sqrt(.5);}//rotate point by eightRotation (part of hg_sdf)
//real and imaginary parts for polar z
float arg(vec2 a){return atan(a.y,a.x);}
vec2 c2p(vec2 a){return vec2(arg(a),le(a));}
vec2 p2c(vec2 a){return vec2(co(a.x),si(a.x))*a.y;}
float real(vec2 z){return p2c(z).x;return z.s*co(z.t);}
float imag(vec2 z){return p2c(z).y;return z.s*si(z.t);}

vec2 crCo(vec2 a){return sqrt(le(a)+c2(a.x));}//core of complex root function,lacks sign adjustment and scaling!
vec2 ciCo(vec2 u,vec2 z,float r){return vec2(u.y-u.x+r*r,2.*z.x*z.y);}//core of complex root function,for 3 inverse trigs

//param2&2*x complex number polar transforms;[Principal branch==0th branch] is implied unless Cth branch can be set explicity.
vec2 sqc(vec2 a){float n=a.x+length(a);return vec2(n,a.y)/sqrt(2.*n);}//sqrt(z)-sqrt((sqrt(a^2+b^2)+a)/2)+sgn(b)sqrt((sqrt(a^2+b^2)-a)/2)i,complex root
vec4 sqc(vec4 a){vec4 c=vec4(crCo(a.xy),crCo(a.zw));c.yw*=sign(a.yw);return c*.5;}//parallel sqrt(z)
vec2 po2c(vec2 c){vec2 d=c*c;return vec2(d.x-d.y,2.*c.x*c.y);}//vec2 po2c(vec2 a){return muc(a,a);return vec2(sub(a*a),2.*a.x*a.y);}//complex square
vec2 po3c(vec2 z){float p=z.x*z.x,q=z.y*z.y;return z*vec2(p-3.*q,3.*p-q);}//z*z*z=a*(a*a-3*b*b)+b*(3*a*a-b*b)*i,complex cube
vec2 recc(vec2 a){if(a.x==0.)return vec2(1e10);return c2(a)/dd(a);}//reciprocal/inverse of z;1/z=(a-b*i)/(a*a+b*b),inverse of z;
vec2 lgc(vec2 a){a=c2p(a);a.x=log(a.x);return a;}//return vec2(log(dd(a))*.5,arg(a));}
vec4 lgc(vec4 a){return vec4(lgc(a.xy),lgc(a.zw));}//parallel lgc()
vec2 suc(vec2 a,vec2 b){return a-b;}vec2 adc(vec2 a,vec2 b){return a+b;}///complex addition is trivial
vec2 muc(vec2 a,vec2 b){return a*b.x+perp(a)*b.y;}//return a*mat2(b.x,-b.y,b.yx);}//complex multoplication
vec2 muc(vec2 a,vec2 b,vec2 c){return muc(muc(a,b),c);}//z*w*x=ace-bde-adf-bcf+(acf-bdf+ade+bce)*i//complex mult
vec2 muc(vec4 a){return muc(a.xy,a.zw);}//parallel mult
vec2 dic(vec2 a,vec2 b){if(a.x==0.)return vec2(65535.);return a*mat2(b,-b.y,b.x)/dd(b);}//return(a*b.x-perp(a)*b.y)/dd(b);}//complex division
vec2 lgc(vec2 a,vec2 b){return dic(lgc(b),lgc(a));}//principal branch of the logarithm base b of z,b is complex;
vec2 lgc(vec2 a,float c){return vec2(log(dd(a))*.5,arg(a)+c*tau);}//Cth logarithm-base-e-branch of z,0th==principal,log   z=log(a^2+b^2)/2+(arg(z)+n2p)i
vec2 exc(vec2 a){return cs(a.y)*exp(a.x);}//pow(exp,a)-pow(eul,a)-pow(eul,a(cos(b)+sin(b)*i))
//vec2 lgc(vec2 a,float c){a=c2p(a);return vec2(log(a.x),a.y+c*tau);}//should be the same!
vec2 poc(vec2 a,vec2 b){return exc(muc(b,lgc(a)));}//pow(b,z)-exp(b*log(a))//0th==principal   branch of pow(z,W)
vec2 poc(vec2 w,vec2 z,float c){return exc(muc(w,lgc(z,c)));}//Cth branch of pow(z,w)
vec2 lgc(vec2 a,vec2 b,float c){return dic(lgc(b,c),lgc(a));}//Cth logarithm-base-a-branch of b,0th==principal,log_b z=log(b)/log(a)
vec2 wrtc(vec2 a,vec2 b){return exc(dic(lgc(b),a));}//0th==principal   branch of pow(z,(1/w))
vec2 wrtc(vec2 a,vec2 b,float c){return exc(dic(lgc(b,c),a));}//Cth branch of pow(z,(1/w))
//vec2 sqc(vec2 z){vec2 c=sqrt((length(z)+c2(z.x))*.5);c.y*=sign(z.y);return c;}//significantly worse near [.y=0.&&x>0]
vec2 sic(vec2 z){return .5*cs(z.x).yx*(exp(z.y)+c2(exp(-z.y)));}//vec2 sic(vec2 z){return vec2(0.5*sin(z.x)*(exp(z.y)+exp(-z.y)),.5*cos(z.x)*(exp(z.y)-exp(-z.y)));}
//obsoleted  vec2 sic(vec2 c){vec2 d=exp(c2(c.y));return vec2(sin(c.x)*(d.x+d.y)*.5,cos(c.x)*(d.x-d.y)*.5);}
vec2 si2c(vec2 c){vec2 d=vec2(exp(c.y),1);return vec2(sin(c.x)*(d.x+d.y)*.5,cos(c.x)*(d.x-d.y)*.5);}
vec2 coc(vec2 z){return .5*c2(cs(z.x))*(exp(z.y)+c2(exp(-z.y)));}

//obsoleted vec2 coc(vec2 z){ return vec2(0.5*cos(z.x)*(exp(z.y)+exp(-z.y)),-0.5*sin(z.x)*(exp(z.y)-exp(-z.y)));}
//obsoleted vec2 coc(vec2 c){vec2 d=exp(c2(c.y));return vec2(cos(c.x)*(d.x+d.y)*.5,-sin(c.x)*(d.x-d.y)*.5);}
vec2 ta2c(vec2 c){vec2 d=exp(c2(c.y));float e=cos(c.x),s=(d.x-d.y)*.5;return vec2(sin(c.x)*e,s*(d.x+d.y)*.5)/(e*e+s*s);}
vec2 tac(vec2 z){return dic(sic(z),coc(z));}//tan(z)-sin(z)/cos(z),complex tangent==ta2c()
vec2 cotc(vec2 z){return dic(coc(z),sic(z));}//cot(z)-cos(z)/sin(z),complex cotangent
////hyperbolics
//sinh z=sinh(a)cos(b)+cosh(a)sin(b)i,hyperbolic sine
vec2 sihc(vec2 z){return .5*cs(z.y)*(exp(z.x)+c2(exp(-z.x)).yx);}//vec2 sinh(vec2 z){ return 0.5*vec2((exp(z.x)-exp(-z.x))*cos(z.y),(exp(z.x)+exp(-z.x))*sin(z.y));}
//vec2 sinh2c(vec2 z){z=c2(z).yx;return sic(z);}//close to sinhc but not the same,phase is off nicely
//cosh(z)-cosh(a)cos(b)+sinh(a)sin(b)i,hyperbolic cosine(swivel-rotate works fine here)
vec2 cohc(vec2 z){return coc(c2(z.yx));}//vec2 cosh(vec2 z){ return vec2(0.5*(exp(z.x)+exp(-z.x))*cos(z.y),0.5*(exp(z.x)-exp(-z.x))*sin(z.y));}
//gl2.0 vec2 tahc(vec2 z){return dic(sinh(z),cosh(z));}//tanh(z)-sinh(z)/cosh(z),hyperbolic tangent
//gl2.0 vec2 cothc(vec2 z){return dic(cosh(z),sinh(z));}//coth(z)-cosh(z)/sinh(z),hyperbolic cotangent
//gl2.0 sechc(vec2 z){return recc(cosh(z));}//sech(z)-1/cosh(z),hyperbolic secant
//gl2.0 vec2 cschc(vec2 z){return recc(sinh(z));}//csch(z)-1/sinh(z),hyperbolic cosecant
//cosh(x)==(pow(e,x)+pow(e,-x)*.5
//sinh(x)==(pow(e,x)-pow(e,-x)*.5
//
//these look stranger than they likely shozld,possibly buggy
//arsic is WAY too noisy.
vec2 arsic(vec2 z){return c2(lgc(perp(z)+sqc(c2(po2c(z.yx)))+vec2(1,0)).yx);}//arsic(z)--log(a*i-b+sqrt(1+b*b-a*a-2abi))*i,inverse sine
//vec2 arsic(vec2 z){vec2 a=sqc(vec2(1.0+z.y*z.y-z.x*z.x,-2.0*z.x*z.y));a=lgc(vec2(-z.y+a.x,z.x+a.y));return vec2(a.y,-a.x);}
vec2 arcoc(vec2 z){return-c2(lgc(z+c2(sqc(po2c(z.yx)+vec2(1,0)).yx)).yx);}//arcoc(z)-log(a+bi-sqrt(1+b^2-a^2-2abi)i)i,inverse cosine
//vec2 arcoc(vec2 z){ vec2 a=sqc(vec2(1.0+z.y*z.y-z.x*z.x,-2.0*z.x*z.y));a=lgc(vec2(z.x+a.y,z.y-a.x));return vec2(-a.y,a.x);}
vec2 csec(vec2 z){return recc(coc(z));}//sec(z)-1/cos(z),complex __secant==complex inverse of complex cosine
vec2 ccsc(vec2 z){return recc(sic(z));}//csc(z)-1/sin(z),complex cosecant==complex inverse of complex __sine
//i am not too sure about the next 5 inverses;likely made some silly error,needs debugging
//arcotc(z)-i*(log((a^2+b^2-b-ai)/(a^2+b^2))-log((a^2+b^2+b+ai)/(a^2+b^2)))*.5,inverse cotangent
//arcotc/(seems to be bad code
vec2 arcotc(vec2 z){float r=z.x*z.x+z.y*z.y;return sub2(c4(lgc((vec4(z.yx,-z.yx)+vec4(r,0,r,0)).yxzw/r)))*.5;}//vec2 arcotc(vec2 z){ float p=z.x*z.x;float q=z.y*z.y;float r=p+q;vec2 a=lgc(vec2(p+q-z.y,-z.x)/r);vec2 b=lgc(vec2(p+q+z.y,z.x)/r);return vec2(b.y-a.y,a.x-b.x)/2.0;}
//artac(z)-i*(log(1+b-ai)-log(1-b+ai))*.5 ,inverse tangent
//tahc(artanh(c))has some symmetry,but may still be broken
//artac appears broken
vec2 artac(vec2 z){return sub2(c4(lgc(vec4(1,0,1,0)-c2(z.yx).xyxy).yxwz))*.5;}//vec2 artac(vec2 z){ vec2 a=lgc(vec2(1.0+z.y,-z.x));vec2 b=lgc(vec2(1.0-z.y,z.x));return vec2(b.y-a.y,a.x-b.x)/2.0;}
//arcsec(csec(c))arcsec(ccsc(c))looks almost good
//arcsec(z)--log((a+sqrt((a^2+b^2)^2-a^2+b^2+2abi)i-bi)/(a^2+b^2))i,inverse secant
vec2 arcsec(vec2 z){vec2 u=vec2(z.x*z.x,z.y*z.y);float r=su(u);return c2(lgc(sub2(c4(vec4(z,sqc(ciCo(u,z,r)).yx)))).yx)+vec2(0.,log(r));}//vec2 arcsec(vec2 z){ float p=z.x*z.x;float q=z.y*z.y;float r=p+q;vec2 a=sqc(vec2(r*r-p+q,2.0*z.x*z.y));a=lgc(vec2(z.x-a.y,a.x-z.y));return vec2(a.y,log(r)-a.x);}
//arccsc(csec(c))arccsc(ccsc(c))looks good
//arccsc(z)--log((sqrt((a^2+b^2)^2-a^2+b^2+2abi)+b+ai)/(a^2+b^2))i,inverse cosecant
vec2 arccsc(vec2 z){vec2 u=vec2(z.x*z.x,z.y*z.y);float r=su(u);return c2(lgc(z.yx+sqc(ciCo(u,z,r))).yx)+vec2(0.,log(r));}//vec2 arccsc(vec2 z){ float p=z.x*z.x;float q=z.y*z.y;float r=p+q;vec2 a=sqc(vec2(r*r-p+q,2.0*z.x*z.y));a=lgc(vec2(a.x+z.y,a.y+z.x));return vec2(a.y,log(r)-a.x);}
//arcsch(z)-log((sqrt((a^2+b^2)^2+a^2-b^2-2abi)+a-bi)/(a^2+b^2)),//inverse hyperbolic cosecant
vec2 arcsch(vec2 z){vec2 u=z*z;float r=su(u);return lgc(c2(z)+sqc(ciCo(u.yx,z,r)))-vec2(0,log(r));}//vec2 arcsch(vec2 z){ float p=z.x*z.x;float q=z.y*z.y;float r=p+q;vec2 a=sqc(vec2(r*r+p-q,-2.0*z.x*z.y));a=lgc(vec2(a.x+z.x,a.y-z.y));return vec2(a.x-log(r),a.y);}
////inverse hyperbolics
//arsinh looks messy,likely broken
vec2 arsinh(vec2 z){return lgc(z+sqc(po2c(z)+vec2(1,0)));}//arsinh(z)-log(a+bi+sqrt(a^2-b^2+1+2abi)),inverse hyperbolic sine
//arsinh looks almost okay,likely broken
vec2 arcosh(vec2 z){return lgc(z+muc(sqc(z.xyxy+vec4(1,0,-1,0))));}//arcosh(z)-log(a+bi+sqrt(a+1+bi)sqrt(a-1+bi)),inverse hyperbolic cosine
//artanh(z)-log((1-a^2-b^2+2bi)/(1+a^2+b^2-2a))/2,inverse hyperbolic tangent
//artanh(artac(c))is uniform ,artac(artanh(c))is white
vec2 artanh(vec2 z){float r=1.-su(z*z);z*=2.;return .5*lgc(-vec2(r,z.y)/(r+z.x));}//vec2 artanh(vec2 z){float r=z.x*z.x+z.y*z.y;return lgc(vec2(1.0-r,2.0*z.y)/(1.0+r-2.0*z.x))/2.0;}
//cothc(arcoth(c))looks somehwat passable
//arcoth(z)-log((a^2+b^2-1-2bi)/(a^2+b^2-2a+1))/2,inverse hyperbolic cotangent
vec2 arcoth(vec2 z){;float r=z.x*z.x+z.y*z.y-1.;z*=-2.;return .5*lgc(vec2(r,z.y)/(r+2.+z.x));}//vec2 arcoth(vec2 z){ float r=z.x*z.x+z.y*z.y;return lgc(vec2(r-1.0,-2.0*z.y)/(r-2.0*z.x+1.0))/2.0;}
//arsech(z)-log((sqrt(a^2-b^2-(a^2+b^2)^2-2abi)+a-bi)/(a^2+b^2)),inverse hyperbolic secant
vec2 arsech(vec2 z){float r=su(z*z);return lgc(c2(z)+muc(sqc(vec4(-r,0,r,0)+c2(z).xyxy)))-vec2(log(r),0);}//ok-ollj

//inspired by https://www.shadertoy.com/view/4tG3Wh  
#define hfrac vec2 h){h=fract(h)
#define gthv greaterThan(h,vec2
#define floatbool2);return float(b.x==b.y);}
float checkerBool(hfrac;return float(h.x>.5==h.y>.5);}
//checkerBool2()might be faster than checkerBool()//xy are independent
float checkerBool2(hfrac;bvec2 b=gthv(.5   )floatbool2
//checkerBoolT oscillates xy comparators over time.
float checkerBoolT(hfrac;bvec2 b=gthv(cos(iTime)*.45+.5)floatbool2
//how to transform this to a non-boolean solution with smooth borders?
//multiply with a smoothstep?
vec4 demoComplex(vec2 u,vec2 m,vec2 n
){u=c2p(u/4.);m=c2p(m/4.);u.x/=pi;m.x/=pi
 ;//u=sqc(u);//u=sqc(vec4(u,m)).xy
 ;//u=suc(u,m);//u=adc(u,m);
 ;u=dic(u,m);//u=muc(u,m)
 ;//u=po2c(u)
 ;//u=po3c(u)
 ;//u=poc(u,m);//u=poc(u,m,n.x)
 ;//u=recc(u)
 ;//u=lgc(u);//u=lgc(u,m);//u=lgc(u,n.x);//u=lgc(u,m,n.x)
 ;//u=exc(u)
 ;//u=wrtc(u,m);//u=wrtc(u,m,n.x)
 ;//u=si2c(u)//;u=sic(u) ;u=coc(u)
 ;//u=cotc(u)
 ;//u=tac(u);//u=ta2c(u);// cotc(u)
 ;//u=sihc(u);u=cohc(u);u=tahc(u);u=cothc(u);u=sechc(u);u=cschc(u)
 ;//u=arcoc(coc(u))
 ;//u=csec(u);//u=ccsc(u);//u=arcotc(u);//u=artac(u)
 ;//u=arcsec(u);//u=arccsc(u);//u=arcsch(u)
 ;//u=arsinh(u);//u=arcosh(u);//u=artanh(u);//u=arcoth(u);//u=arsech(u)
 ;//u=p2c(u)
 ;float c=checkerBool(u)//1.- for sechc()
 ;vec4 a=vec4(fract(u*2.),c,c*.9)
 ;a.xyz*=a.w
 ;return a
 ;}
 
//---param3MAT
//SF3() is a hot mess to be simplified a lot! //only allows for vec1 ot vecmax
//explicit namespace fold for scalar operations
#define fi3(m,z,a,b,c) {return z(m(a),m(b),m(c));}
#define Q3(z,a,b,c){return z(a,b,c);}
#define SF3(n,y,z,a,b,c) n y(n a,n b,n c)fi3(n,z,a,b,c) n y(n a,n b,vec1 c)fi3(n,z,a,b,c) n y(vec1 a,vec1 b,n c)fi3(n,z,a,b,c) n y(n a,vec1 b,n c)fi3(n,z,a,b,c) n y(n a,vec1 b,vec1 c)fi3(n,z,a,b,c) n y(vec1 a,n b,n c)fi3(n,z,a,b,c) n y(vec1 a,n b,vec1 c)fi3(n,z,a,b,c)
#define scalar3(y,z) vec1 y(vec1 a,vec1 b,vec1 c)Q3(z,a,b,c) SF3(vec2,y,z,a,b,c) SF3(vec3,y,z,a,b,c) SF3(vec4,y,z,a,b,c)
scalar3(mx,mix)
//---param1AD Automatic Differenciation (needs param2MAT)
//todo AD for;tan,asin,acos,atan,reciprocal
//memo museum: for consistency structure changed: d->v00->v11;d1->v01->w12;d2->v02->w13
//...the swivels oof d; are .xd ,the swivels of v0N (and [w*]) are .ab
vec1 rec(float a){return(a==0.)?a:1./a;}//return [1/a] ,work safe//sqrt()and divisions require a worksave reciprocal operator:
#define sqD(h)h sq(h a){vec1 q=sq(a.a);return h(q,.5*rec(q)*a.b);}
#define cmD(h,i) h cmd(w11 a,i b){return h(a.a,mu(a.b,b));}
cmD(w11,vec1)cmD(w12,vec2)cmD(w13,vec3)cmD(w14,vec4)//cmd() is special case of component-wise-multiplication,special because a.x==1: is subroutine of abs()
//i am not too sure about my implementation on fr();
//the hell,just use mo()instead,mo()has VERY nice first derivatives!
//reminder that the fract function's first derivative has a "kick" on (mod(a,1)-=.0)
vec1 jud(vec1 a){return mx(a,-1.,st(fr(a),0.)*st(0.,fract(a)));}
vec2 jud(vec2 a){return vec2(jud(a.x),jud(a.y));}
vec3 jud(vec3 a){return vec3(jud(a.x),jud(a.y),jud(a.z));}
vec4 jud(vec4 a){return vec4(jud(a.x),jud(a.y),jud(a.z),jud(a.w));}
//i should definitely define this by fract,and not by floor.
//because fm-modulation taught me that i need fract()a lot more than floor()
#define frD(h)h fr(h a){return h(fl(a.a),jud(a.b));}
#define abD(h)h ab(h a){return cmd(w11(ab(a.a),sg(a.a)),a.b);}//w13(abs(a.x),sign(a.x)*a.d);}
#define siD(h)h si(h a){return h(si(a.a),mu(co(a.a),a.b));}
#define coD(h)h co(h a){return h(co(a.a),mu(ne(si(a.a)),a.b));}
#define lgD(h)h ln(h a){return h(ln(a.a),di(a.b,a.a));}
#define exD(h)h ex(h a){vec1 x=exp(a.a);return h(x,x*a.b);}
#define DA1(h)h(w11)h(w12)h(w13)h(w14)
DA1(abD)DA1(siD)DA1(coD)DA1(lgD)DA1(exD)DA1(sqD)DA1(frD)//because different functions have unique derivatives,
//fl(a)=a-fr(a); hasvery slightly lower precision?rarely relevant.fract()has great precision on float!
#define fl(a) su(a,fr(a))
//---Param2AD are trickier: https://en.wikipedia.org/wiki/Multivariable_calculus
//There are exponential many permutations of mo()mx()mi()ma()
//AD_mo() ideally should be replaced by fr(),but that adds 1dvd()and 1mu(),BUT DA_mo(a,b) outperforms mu(fr(di(a,b)),b),via [reciprocal product rule]
#define moD(h,i)h mo(h a,i b){return h(mo(a.a,b  ),fr(a.b));}
#define moE(h,i)h mo(h a,i b){return h(mo(a.a,b.a),fr(a.b));}
#define moF(h,i)h(w11,i)h(w12,i)h(w13,i)h(w14,i)
moF(moD,vec1)moF(moE,w11)
#define miG(r,h,j) r mo(h a,r b){return r(mod(a.a,b.a),j(0));}
miG(w14,w13,vec4)miG(w14,w12,vec4)miG(w14,w11,vec4)miG(w13,w11,vec3)miG(w13,w12,vec3)
w13 mo(vec1 a,w13 b){return w13(mod(a,b.a),vec3(0));}
w12 mo(vec1 a,w12 b){return w12(mod(a,b.a),vec2(0));}
w11 mo(vec1 a,w11 b){return w11(mod(a,b.a),0.);}
//substraction is simple,because "differentiation is integration"; https://en.wikipedia.org/wiki/Cauchy_integral_formula
//...making the implementation of the chain rule much simpler for; https://en.wikipedia.org/wiki/Translation_(geometry)
//BUT if we substract a struct da from a vec1 or w11,we must negate the .b part.
//i possibly messed this one up by not flipping the .a part,but i an not sure there.
#define suNN(h,i,j)h su(i a,j b){return h(su(a.a,b.a),a.b);}h su(j a,i b){return h(su(a.a,b.a),-b.b);}
suNN(w12,w12,w11)suNN(w13,w13,w11)suNN(w13,w13,w12)//the leftmost param equals the larger param of the other 2 params
suNN(w14,w14,w11)suNN(w14,w14,w12)suNN(w14,w14,w13)//..the rightmost param is always smaller than the middle param
#define suNM(t)t su(t a,vec1 b){return t(su(a.a,b),a.b);}t su(vec1 a,t b){return t(su(a,b.a),-b.b);}
#define suMM(t)suNM(t)t su(t a,t b){return t(su(a.a,b.a),su(a.b,b.b));}
suMM(w11)suMM(w12)suMM(w13)suMM(w14)
//in the end mix() has an identity,and that identity does translate into DA:
#define mixd(a,b,c) ad(mu(c,su(a,b),a)
//a big issue of this is that mixd() and mind() where defined as MAT-functions,vut using the AD-labeling
//so is just deleted that junk code
//I define mind()and=maxd(-,-)instead of maxd()and=mind(-,-)
//because min()is far more common than max(),for z-buffering.
//negation identity:max(a,b)--min(-a,-b),requires struct negation
//addition is negated substraction.
#define ad(a,b) su(a,ne(b))
//AD_mu simplifies contextually ,scalar multiplication is tautological product_rule.
#define adM2(h) h mu(h a,vec1 b){return h(mu(a.a,b),mu(a.b,b));}
#define adM1(h) adM2(h)h mu(vec1 a,h b){return mu(b,a);}
#define atta(h,i,j) h mu(i a,j b){return h(mu(a.a,b.a),ad(mu(a.b,b.a),mu(a.a,b.b)));}
#define attA(h) atta(h,h,w11)atta(h,h,h)atta(h,w11,h)
adM1(w11)adM1(w12)adM1(w13)adM1(w14)
atta(w11,w11,w11)attA(w12)attA(w13)attA(w14)//oh i really like this attA()-way of folding it
DAm2 mu(DAm2 p,vec3 s){return DAm2(mu(p.x,s.x),mu(p.y,s.y),mu(p.z,s.z));}
DAm1 mu(DAm1 p,vec2 s){return DAm1(mu(p.x,s.x),mu(p.y,s.y));}
DAm0 mu(DAm0 p,vec1 s){return DAm0(mu(p.x,s));}
//reciprocal derivatives are most confusing.
#define rXX(h) h di(h a,vec1 b){return h(di(a.a,b),di(a.b,b));}h di(vec1 a,h b){return h(di(a,b.a),di((mu(ne(a),b.b)),mu(b.a,b.a)));}
rXX(w11)rXX(w12)rXX(w13)rXX(w14)
//inverse scalar multiplication is a tautolotgy.
//some of these may be nonsensical
#define rXY(h,i,j) i di(h a,i b){return i(di(a.a,b.a),di((mu(ne(a.a),b.b)),(b.a*b.a)));}i di(i a,j b){return i(di(a.a,b.a),di((su(mu(a.b,b.a),mu(a.a,b.b))),(mu(b.a,b.a))));}
rXY(w13,w12,w11)
rXY(w11,w13,w13)
rXY(w12,w13,w11)
rXY(w11,w14,w14)//just guessing this line
rXY(w12,w14,w11)//just guessing this line
//All exponential functions utilize their Base_E_identity:...which is still not efficient,but comes down to O(exp(n*log(n)))
//pow (x,y)-=exp(log(x)*y)//baseE exponential and logarythmic functions.
#define po(x,y)ex(mu(ln(x),x))
/*
//It gets trickier with functions that take 3 parameters:,applying the
//https://en.wikipedia.org/wiki/Triple_product_rule
//Euclidean distance (pythagorean theorem)with first derivatives.
#define v0q vec1 q=length(vec2(x.a,y.a))
#define ll2 q,(x.b*x.a+y.b*y.a)*rec(q));}
w11 le(w11 x,w11 y){v0q;return w11(ll2
w12 le(w12 x,w12 y){v0q;return w12(ll2
w13 le(w13 x,w13 y){v0q;return w13(ll2
w13 lengthw13(DAm2 u){return le(u.x,u.y);}//2*3domain
//above is planar length 2 input parameters.below is 3d length.
w13 le(w13 x,w13 y,w13 z){float q=length(vec3(x.a,y.a,z.a));return w13(q,(x.b*x.a+y.b*y.a+z.b*z.a)*rec(q));}
w13 le(DAm2 u){return le(u.x,u.y,u.z);}//3*3domain
//the utility of a length()function is clear.*/
//su(ab(p),s)equals a translation away from the origin by [s] AND mirroring at origin,via abs()
//...so it it clamps a an implicit surface to a limited "thickness" (in 3 domains),also translating its first derivatives.
w12 suab(w12 p,vec1 s){return su(ab(p),s);}
w11 suab(w11 p,vec1 s){return su(ab(p),s);}
w13 suab(w13 p,vec1 s){return su(ab(p),s);}
DAm2 suab(DAm2 p,vec3 s){return DAm2(suab(p.x,s.x),suab(p.y,s.y),suab(p.z,s.z));}
DAm1 suab(DAm1 p,vec2 s){return DAm1(suab(p.x,s.x),suab(p.y,s.y));}
DAm0 suab(DAm0 p,vec1 s){return DAm0(suab(p.x,s));}


//spheretracker /ePr is referenced by bicapsule() and other unsigned distances
//ideally only a 4d marcher is defined,with special cases for less domains
//but usually a 4d object only is traced as its 3d shadow/hyperslice.
const float iterRm=256.;
const float eRm=.00001;
const float zFar=20000.;
float df(vec3 p);
vec3 dNormal(vec3 p){const vec2 e=vec2(.005,0);return norma(vec3
 (df(p+e.xyy)-df(p-e.xyy)
 ,df(p+e.yxy)-df(p-e.yxy)
 ,df(p+e.yyx)-df(p-e.yyx)));}
 
vec4 trace(v33 a
){float t=0.
 ;for(float i=0.;i<iterRm;++i
 ){float d=df(a.a)
  ;if(d<eRm)return vec4(a.a,i);
  ;if(t>zFar)return vec4(0)
  ;a.a+=d*a.b*.5//this marther has its first step overstep way too often,doubling lipschitz evades this(poorly)
  ;t+=d;}return vec4(a.a,iterRm);}
vec4 trace(vec3 a,vec3 b){return trace(v33(a,b));}//legacy compatible




//"crossproduct" and "determinant" are related,cross() is the bilin()* of a lin()_determinant()
//therefore i can fold the detemrinant() mnamespace onto the cross() namespace.
//a cross() has 2 inpouts,where a determinant() has 1 input
//we generalize "crossproduct" into its ternary: "find the only vector that in a 90deg angle to all inputs"
vec2 cr(vec2 a){return a.yx*vec2(-1,1);}//rotBy4
vec3 cr(vec3 a,vec3 b){return cross(a,b);}
//vec4 cr(vec4 a,vec4 b,vec4 c){
//solve LinearEquation for   dot(a,d)=0;dot(b,d)=0;dot(c,d)=0;with additional constrains to length(d)==1
//}
//[a bilinear (2 input vectors) product with a vector result only exists in 3d and 7d,in 7f it has more than 2 results (handedness,signs)
//7d_dotproduct() is the octonion_pairing to 3d_quaternion.
float det2d(vec2 a,vec2 b){return dot(a,cr(b));}//a.x*b.y-a.y*b.x//2d determinant(mat2(a,b))==det2d(a,b)==perpendicular dotproduct perpdot dotperp

//hashes are named by output type,NEVER by input type
//hfd() is slower but slighly better than fractSin()
//hfd1 mirrors at y=x and has strong banding on diagonals.
//fract(dot(1031))hash summer 2018 seems ot be generally superior to fract(sin())hashes
vec3 g3(vec1 a){return vec3(a);}
vec3 g3(vec2 a){return a.xyx;}
vec3 g3(vec3 a){return a;}
vec4 g4(vec1 a){return vec4(a);}
vec4 g4(vec2 a){return a.xyxy;}
vec4 g4(vec3 a){return a.xyzy;}
vec4 g4(vec4 a){return a;}
//[hfd*] hashes by David Hoskins,Creative Commons Attribution-ShareAlike 4.0 International Public License
//parent https://www.shadertoy.com/view/4djSRW
#define hs vec4(.1031,.1030,.0973,.1099)
//#define HASHSCALE3 vec3(443.897,441.423,437.195,444.129)//For smaller input rangers like audio tick or 0-1 UVs use these...
#define hout1(a)fract((a.x+a.y)*a.z)
#define hout2(a)fract((a.xx+a.yz)*a.zy)
#define hout3(a)fract((a.xxy+a.yzz)*a.zyx)
#define hout4(a)fract((a.xxyz+a.yzzw)*a.zywx)
#define h3mid(a)((a)+dot(a,a.yzx+19.19))
#define h4mid(a)((a)+dot(a,a.wzxy+19.19))
//only hash4 takes in vec4.all hash functions take vey1,v3c2,vec3 in (a vec4 generalizations is a mild overkill for most vec2,vec3 contexts)
#define hfd1(a)hout1(h3mid(fract(hs.x*g3(a))))
#define hfd2(a)hout2(h3mid(fract(hs.xyz*g3(a))))
#define hfd3(a)hout3(h3mid(fract(hs.xyz*g3(a))))
#define hfd4(a)hout4(h4mid(hs*g4(a)))
//not sure if its this shaders fault,likely a fract()error,but the avobe hsh gives bad lines.
//float hfd1(float n){ return fract(sin(n)*1e4);}

//hash by dr2,incompatible with a more common hash,good for fast fbm with normals (labeled: h41-noise)
#define vec1 float
vec1 mx(vec1 a,vec2 b){return mix(b.x,b.y,a);}
vec1 bilin(vec4 a,vec2 b){return mix(mx(b.x,a.xy),mx(b.x,a.zw),b.y);}
#define herm32(a) ((a)*(a)*(3.-2.*(a)))
//noise by dr2 is a union of "penguins": https://www.shadertoy.com/view/4lfBWB
//and "train ride":                      https://www.shadertoy.com/view/4s2Sz3
//and "Books and Stairs 2"               https://www.shadertoy.com/view/MtsfRl
const vec1 cHashM=43758.54;
vec4 hSeed=vec4(0,1,57,58);//vec3(0,37,39,41); //vec4(0,1,57,113);
vec1 hash1(vec2 p){return fract(sin(dot(p,hSeed.yz))*cHashM);}
vec1 hash1(vec3 p){return fract(sin(dot(p,hSeed.yzw))*cHashM);}
vec2 hash2(vec1 p){return fract(sin(p+vec2(0,1))*cHashM);}
vec2 hash2(vec2 p){return fract(sin(vec2(dot(p,hSeed.yz),dot(p+vec2(1,0),hSeed.yz)))*cHashM);}
vec4 hash4(vec3 p){vec2 e=vec2(1,0);return fract(sin(vec4(dot(p,hSeed.yzw),dot(p+e.xyy,hSeed.yzw),dot(p+e.yxy,hSeed.yzw),dot(p+e.xxy,hSeed.yzw)))*cHashM);}
vec4 hash4(vec1 p){return fract(sin(p+hSeed)*cHashM);}
vec1 noise1(vec1 p){return mx(herm32(fract(p)),hash2(floor(p)));}
vec1 noise1(vec2 p){vec2 f=floor(p);p=herm32(fract(p));return mx(p.x,mix(hash2(f),hash2(f+vec2(0,1)),p.y));}
vec1 noise1(vec3 p){vec3 f=floor(p);p=herm32(fract(p));return bilin(mix(hash4(f),hash4(f+vec3(0,0,1)),p.z),p.xy);}
vec3 noise3(vec2 p){vec2 f=fract(p),g=f*f,u=g*(3.-2.*f);vec4 h=hash4(dot(floor(p),hSeed.yzw.xy))
 ;return vec3(h.x+(h.y-h.x)*u.x+(h.z-h.x)*u.y+(h.x-h.y-h.z+h.w)*u.x*u.y,30.*g*(g-2.*f+1.)*(vec2(h.y-h.x,h.z-h.x)+(h.x-h.y-h.z+h.w)*u.yx));}
//gradient shaded volumetric animated noise,labeled [afo-tri-noise],from "dust storm" by @stormoid
//has strong diagonals and strong short periodicity.
#define perm2(k,a) k(a.x+k(a.y))
#define perm3(k,a) k(a.z+perm2(k,a.xy))
vec3 afo3(vec3 p){return vec3(perm2(u5cos,p.zy),perm2(u5cos,p.zx),perm2(u5cos,p.yx));}
float noise1t(vec3 p,float spd//triangle-interpolation noise.
){float z=1.4,r=0.
 ;p=p*9.+vec3(7,13,21)//optionally evade the strong [y=x mirror] that afo3() has
 ;vec3 b=p
 ;for(float i=0.;i<4.;i++//multi-octaves,but the afo3(()function also implies a sqivel-rotation.
 ){vec3 dg=afo3(b*2.)
  ;p+=(dg+iTime*spd);b*=1.8;z*=1.5;p*=1.2
  ;r+=perm3(u5cos,p)/z //a weird way of using define,deal with it
  ;b+=.14;};return r;}//the hyperplanes are aligned to the lattice,and because of that alignment the animation does not look too "random"
//noronoi/cellular
//noise-open-challenge:
//there exist ways to do a 2pass of 2 voronoi,first pass is 3x3 square lattice 9tap,with 3 buffered values.
//,second pass is 5x5 square lattice 25 tap.
//and the result is a [shortest distance to cell border]
//and i would like to have this here,with a planar distance and/or a 3d distance to a cell border
v33 ff(vec3 a){return v33(fract(a),floor(a));}//BUT here mat3 would just waste memory and mat32 mat23 are less comatible.
v22 ff(vec2 a){return v22(fract(a),floor(a));}
vec2 ff(float a){return vec2(fract(a),floor(a));}
v33 su(vec2 a,v33 b){return v33(a.x-b.a,a.y-b.b);}
v33 mu(float a,v33 b){return v33(a*b.a,a*b.b);}
mat3 su(mat3 a,mat3 b){return a-b;}
mat3 addf(mat3 a,vec3 b){return mat3(a[0]+b.x,a[1]+b.y,a[2]+b.z);}

//fast cellular noise,optimized by ollj
//mouse.xy is basically "salting hashes"
//imouse.x sets jitter range[0..1] 0 is squares,1 is maximum jitter. (known bug,iMouse.x-scaling for lower left quadrant is a bit bad here)
//imouse.z sets hash dividend,is ideally 1/7,but others can be fine too.use like a salt.
//lower left quadrant is 2d input noise2x2x2
//the other 3 quadrants are 3d input noises3x3x3 where .z is iTime.
//the 3 quadrants inputs are swiveled differently to debug 3 orthogonal hyperplanes in one view.
//return value .x is L1,.y is L2 according to [worley noise]
//this uses a lot of mat2,or the v33 struct,and permute()functions similar to ahsima-simplex noise,defers the sorting,for performance.
//Cellular noise ("Worley noise")in 3D in GLSL.
//Copyright (c)Stefan Gustavson 2011-04-19.All rights reserved.
//This code is released under the conditions of the MIT license.
//See LICENSE file for details.
//https://github.com/stegu/webgl-noise
#define mous (iResolution.xyxy-iMouse.xyzw)//flip mouse everywhere,for shadertoy fun
mat3 fr(mat3 a){return mat3(fr(a[0]),fr(a[1]),fr(a[2]));}
//mat3 fl(mat3 a){return mat3(fl(a[0]),fl(a[1]),fl(a[2]));}
mat3 addd(mat3 a,vec3 b){return mat3(a[0]+b.x,a[1]+b.y,a[2]+b.z);}
//mat3 addF(mat3 a,vec3 b){return mat3(a[0]+b,a[1]+b,a[2]+b);}//contextual namespace hell.
mat3 ma3(vec3 a,float b,float c){return mat3(a,vec3(b),vec3(c));}
//th7 is for the 2d noise,should be (iResolution.y/7.),BUT i managed to make other values look fine,too
#define th7 (mous.y/iResolution.y)//usually==1./7.,because mod(a,7)is famous for old LCGs.
//th8 is for the 3d noise,should be (iResolution.y/7.),other values may work,but they tend to barely work at all,try only simple factors of 1/7
#define th8 (iResolution.y/7./mix(1.,4.,mous.y/iResolution.y))//seems to be fine bounds,not too sure.
vec3 mod2893d(vec3 x){return x-fl(x*(1./289.))*289.;}//Modulo 289 without a division (only multiplications)
mat3 mod2893d(mat3 x){return x-fl(x*(1./289.))*289.;}//Modulo 289 without a division (only multiplications)
vec3 mod73d(vec3 x){return x-floor(x*(1.0/th8))*th8;}//Modulo 7 without a division
mat3 mod73d(mat3 x){return x-fl(x*(1.0/th8))*th8;}
//Permutation polynomial:(34x^2+x)mod 289
vec3 permute3d(vec3 x){return mod2893d((34.0*x+1.)*x);}
mat3 permute3d(mat3 x){return mod2893d((34.0*x+1.)*x);}
#define Kcel 1./th8//1/7
#define Kcel2 1./th8/th8//1/(7*7)
#define Kcelo (1.-Kcel)*.5//1/2-Kcel/2
#define Kcelz 1./(th8-1.)//1/6
#define Kcelzo .5-2./(th8-1.)//0.416666666667//1/2-1/6*2
#define jitter mous.x/iResolution.x//smaller jitter gives more regular pattern
mat3 perM3(vec3 p){return mat3(permute3d(p-1.),permute3d(p),permute3d(p+1.));}
mat3 perM3(vec3 p,vec3 b){vec3 p1=permute3d(p+b.x-1.);vec3 p2=permute3d(p+b.y);vec3 p3=permute3d(p+b.z+1.);return mat3(p1,p2,p3);}
#define maa(a,b,c) ma3(pf[0],pf[1].a,pf[2].b)+jitter*c
#define mat3dd(a)a[0]*a[0]+a[1]*a[1]+a[2]*a[2]
#define mat3ddmaa(a,b,c)mat3dd((maa(a,b,mat3(ox3[c],oy3[c],oz3[c]))))
vec3 square(v33 a){return a.a*a.a+a.b*a.b;}
//todo,make it traversable bny getting distance2Border.
vec2 cellular(vec3 P,float m//not to be confuced with a voronoi3d,but can look similar.
){vec3 p=mod2893d(floor(P))
 ;mat3 pf=addf(mat3(1,0,-1,1,0,-1,1,0,-1),fract(P)-.5)
 ;mat3 pp=perM3(permute3d(p.x+vec3(-1,0,1))+p.y)
 ;mat3 p1=perM3(pp[0]+p.z)
 ;mat3 p2=perM3(pp[1]+p.z)
 ;mat3 p3=perM3(pp[2]+p.z)
 ;mat3 ox=fr(p1*Kcel)-Kcelo
 ;mat3 ox2=fr(p2*Kcel)-Kcelo
 ;mat3 ox3=fr(p3*Kcel)-Kcelo 
 ;mat3 oz=fl(p1*Kcel2)*Kcelz-Kcelzo
 ;mat3 oz2=fl(p2*Kcel2)*Kcelz-Kcelzo
 ;mat3 oz3=fl(p3*Kcel2)*Kcelz-Kcelzo 
 ;mat3 oy=mod73d(fl(p1*Kcel))*Kcel-Kcelo
 ;mat3 oyy=mod73d(fl(p2*Kcel))*Kcel-Kcelo
 ;mat3 oy3=mod73d(fl(p3*Kcel))*Kcel-Kcelo
 ;mat3 dy=jitter*oy+pf[1].x
 ;mat3 dy2=jitter*oyy+pf[1].y
 ;mat3 dz=addf(jitter*oz,vec3(pf[2].x,pf[2].y,pf[2].z))
 ;mat3 dz2=addf(jitter*oz2,vec3(pf[2].x,pf[2].y,pf[2].z))
 ;mat3 dx=mat3(pf[0],pf[0],pf[0])+jitter*ox   
 ;mat3 dx2=mat3(pf[0],pf[0],pf[0])+jitter*ox2
 ;mat3 d1=maa(x,x,mat3(ox[0],oy[0],oz[0]))
 ;vec3 d31=mat3ddmaa(z,x,0)
 ;vec3 d32=mat3ddmaa(z,y,1)
 ;vec3 d33=mat3ddmaa(z,z,2)
 ;vec3 d11=dx[0]*d1[0]+dy[0]*d1[1]+dz[0]*d1[2]
 ;vec3 d12=dx[1]*dx[1]+dy[1]*dy[1]+dz[1]*dz[1]
 ;vec3 d13=dx[2]*dx[2]+dy[2]*dy[2]+dz[2]*dz[2] 
 ;vec3 d21=dx2[0]*dx2[0]+dy2[0]*dy2[0]+dz2[0]*dz2[0]
 ;vec3 d22=dx2[1]*dx2[1]+dy2[1]*dy2[1]+dz2[1]*dz2[1]
 ;vec3 d23=dx2[2]*dx2[2]+dy2[2]*dy2[2]+dz2[2]*dz2[2]
 ;mat3 a
 //for a second pass that also gets distance2Border i need these 3 values
 //mg=g;//ID to shortest distance cell (integer vector)
 //mr=r;//vector to shortest distance    md=d;//shortest distance squared==is dot(r,r)
 //Sort out the two smallest distances (F1,F2)
 #if 0
 ;vec3 d1=min(min(d11,d12),d13) //sort out only F1
 ;vec3 d2=min(min(d21,d22),d23)
 ;vec3 d3=min(min(d31,d32),d33)
 ;vec3 d=min(min(d1,d2),d3)//shortest of 9 distances,except its 9*vec3()
 ;d.x=min(min(d.x,d.y),d.z)
 ;return vec2(sqrt(d.x));}//F1 F1
 #else
 ;vec3 d1a=min(d11,d12)//sort out F1 and F2
 ;d12=max(d11,d12)
 ;d11=min(d1a,d13)//Smallest now not in d12 or d13
 ;d13=max(d1a,d13)
 ;d12=min(d12,d13)//2nd smallest now not in d13
 ;vec3 d2a=min(d21,d22)
 ;d22=max(d21,d22)
 ;d21=min(d2a,d23)//Smallest now not in d22 or d23
 ;d23=max(d2a,d23)
 ;d22=min(d22,d23)//2nd smallest now not in d23
 ;vec3 d3a=min(d31,d32)
 ;d32=max(d31,d32)
 ;d31=min(d3a,d33)//Smallest now not in d32 or d33
 ;d33=max(d3a,d33)
 ;d32=min(d32,d33)//2nd smallest now not in d33
 ;vec3 da=min(d11,d21)
 ;d21=max(d11,d21)
 ;d11=min(da,d31)//Smallest now in d11
 ;d31=max(da,d31)//2nd smallest now not in d31
 ;d11.xy=(d11.x<d11.y)?d11.xy:d11.yx
 ;d11.xz=(d11.x<d11.z)?d11.xz:d11.zx//d11.x now smallest
 ;d12=min(min(d21,min(d22,d31)),min(min(d12,d21),min(d22,d32)))
 ;d11.yz=min(d11.yz,d12.xy)//nor in d12.yz
 ;d11.y=min(d11.y,d12.z)//Only two more to go
 ;d11.y=min(d11.y,d11.z)//Done! (Phew!)
 ;return sqrt(d11.xy);}//F1,F2
#endif
//crunched by ollj
//Cellular noise ("Worley noise")in 2D in GLSL.
//Copyright (c)Stefan Gustavson 2011-04-19.All rights reserved.
//This code is released under the conditions of the MIT license of://https://github.com/stegu/webgl-noise
//mod(a,289.)without a division (only multiplications),because [mod(33.,33.)!=0] on too many implementations.
vec3 mob89(vec3 a){return a-floor(a*(1./289.))*289.;}
vec2 mob89(vec2 a){return a-floor(a*(1./289.))*289.;}
v22  mob89(v22  a){return v22(a.a,a.b-floor(a.b*(1./289.))*289.);}//vers ypecial use case
mat2 mob89(mat2  a){return mat2(a[0],a[1]-floor(a[1]*(1./289.))*289.);}//vers ypecial use case
vec3 modth7(vec3 a){return a-floor(a*th7)/th7;}//Modulo 7 without a division
v33  modth7(v33  a){return v33(a.a,(a.b-floor(a.b*th7)/th7)*th7);}//special case for a #define
vec3 permute(vec3 a){return mob89((34.*a+1.)*a);}//Permutation polynomial:(34x^2+x)mod 289
#define tt3(p,f,j,m,a)square(su(j,su(v33(vec3(a),f),mu(m,mu(m,modth7(ff(p*th7)))))))
#define permi(c)p=permute(o+k.b.y+px.c)
#define ppm(d,e,c)r[d]=tt3(p,f,k.a,m,e);permi(c)
//Cellular noise,returning F1 and F2 in a vec2//3x3-hood reduced to 2 permute()as special case voronoi
//m[0..1] sets distortedness for a GOOD LCG (minimal self-similarity)m=1./7.,by sqiveling this parameter,you get+0.7 domain very cheaply
//as in by making m a 3rd parameter,you ALMMOST get cheap cellular3d noise,BUT it is likely a bit flawed,more repetitive,shorter period.
vec2 cellular(vec2 P,float m//this is voronoi without loop.the permute()function marks+1 iteration/tap//is 4tap voronoi in O(3)
){v22 k=mob89(ff(P))
 ;vec3 o=vec3(-1,0,1),f=o+.5,px=permute(o+k.b.x),permi(x)
 ;mat3 r
 ;ppm(0,-.5,y)//it seems that the loop got unrolled and al lits min()fucntions fold into the below
 ;ppm(1,+.5,z)//...//which is quite a lot of symmetry folding,surely gets better performance.
 ;r[2]=min(r[0],r[1])
 ;r[1]=clamp(r[1],r[0],tt3(p,f,k.a,m,1.5))
 ;r[0]=min(r[1],r[2])
 ;r[1]=max(r[2],r[1])
 ;r[0].xy=mix(r[0].yx,r[0].xy,step(r[0].x,r[0].y))
 ;r[0].xz=mix(r[0].zx,r[0].xz,step(r[0].x,r[0].z))
 ;r[0].yz=min(r[0].yz,r[1].yz)//can not be inserted in below line ,because r[0].y is a return value.
 ;r[0].y=min(mi(r[0].yz),r[1].x)
 ;return sqrt(r[0].xy);}
//simplex noise3d (simplex noise is worth it for higher dimensions)
//Original:https://github.com/ashima/webgl-noise/blob/master/src/noise3D.glsl
vec4 permute(vec4 x){return mod(x*x*34.+x,289.);}
float simplex1(vec3 v//ashima simplex3d,early optimizations by @makio64,structure by @ollj  https://www.shadertoy.com/view/Xd3GRf
){const vec2 C=1./vec2(6,3);const vec4 D=vec4(0,.5,1,2)
 ;vec3 i=floor(v+dot(v,C.yyy)),x0=v-i+dot(i,C.xxx),g=step(x0.yzx,x0.xyz),l=1.-g//is a sequence of 4,no parallelization
 ;v33 f=v33(min(g.xyz,l.zxy),max(g.xyz,l.zxy));v3333 d=g3333(x0,su(x0,g333(su(f,g33(C)),D.yyy)))
 ;i=mod(i,289.);v222 F=g222(f);vec3 ns=.142857142857*D.wyz-D.xzx
 ;vec4 p=permute(permute(permute(i.z+vec4(0,F.c,1))+i.y+vec4(0,F.b,1))+i.x+vec4(0,F.a,1)),j=p-49.*floor(p*ns.z*ns.z),k=floor(j*ns.z)
 ;v44 t=ad(mu(v44(k,floor(j-7.*k)),ns.x),ns.y)
 ;vec4 h=1.-abs(t.a)-abs(t.b),a=-step(h,vec4(0))
 ;v44 b=v44(vec4(t.a.xy,t.b.xy),vec4(t.a.zw,t.b.zw))
 ;b=ad(v44(b.a.xzyw,b.b.xzyw),mu(v44(a.xxyy,a.zzww),ad(mu(fl(v44(b.a.xzyw,b.b.xzyw)),2.),1.)))
 ;v3333 q=v3333(vec3(b.a.xy,h.x),vec3(b.a.zw,h.y),vec3(b.b.xy,h.z),vec3(b.b.zw,h.w))
 ;q=muv(q,inversesqrt(ddv(q)));vec4 m=max(.6-ddv(d),0.)
 ;return .5+12.*dot(m*m*m,vec4(dot(q.a,d.a),dot(q.b,d.b),dot(q.c,d.c),dot(q.d,d.d)));}

//fbm of dr2 noise (with multitap normals),is a bit crude,in favor for fast normals.
#define fBm(a) vec2 b=vec2(0,1);for(int i=0;i<a;i++){b=vec2(b.x,0)+vec2(noise1(p),.5)*b.y;p*=2.;}return b.x/(2.-b.y);}
vec1 fbm(vec1 p){fBm(5)vec1 fbm(vec2 p){fBm(5)vec1 fbm(vec3 p){fBm(5)vec1 fbms(vec3 p){fBm(3)
vec1 fbmn(vec3 p,vec3 n){vec4 r=vec4(0,0,0,1);for(int i=0;i<4;i++
){r=vec4(r.xyz,0)+r.w*vec4(vec3(noise1(p.yz),noise1(p.zx),noise1(p.xy)),.5);p*=2.;}return dot(r.xyz,abs(n));}
vec3 VaryNf(vec3 p,vec3 n,vec1 f
){vec2 e=vec2(.1,0);vec3 g=vec3(fbmn(p+e.xyy,n),fbmn(p+e.yxy,n),fbmn(p+e.yyx,n))-fbmn(p,n)
 ;return norma(n+f*(g-n*dot(n,g)));}
float fbma(vec3 p//fbm of ashima simplex (no normals,duh)
){float f
 ;f=.5*(simplex1(p));p=p*2.01
 ;f+=.25*(simplex1(p));p=p*2.02
 ;f+=.125*(simplex1(p));p=p*2.03
 ;f+=.0625*(simplex1(p));p=p*2.04
 ;f+=.03125*(simplex1(p));return f;}

//---hg_sdf http://mercury.sexy/hg_sdf // https://www.shadertoy.com/view/Xs3GRB
//mod must be on top od all unions that include mod()
float pMirror(inout float p,float d){float s=mix(-1.,1.,step(p,0.));p=abs(p)-d;return s;}
float pReflect(inout vec3 p,vec3 n,float o){float t=dot(p,n)+o;p=mix(p-2.*t*n,p,step(t,0.));return mix(-1.,1.,step(t,0.));}
vec2 pMirrorOctant (inout vec2 p,vec2 dist){vec2 s=vec2((p.x<0.)?-1.:1.,(p.y<0.)?-1.:1.);pMirror(p.x,dist.x);pMirror(p.y,dist.y);if(p.y>p.x)p.xy=p.yx;return s;}
//todo,make better column code! this may benefid from my improved pmod() code
#define frflpm(z,y)z frfl(z a){return z(fract(a.a),floor(a.b));}z pmod(z a){return mu(su(frfl(y(a.a/a.b+.5)),y(.5,.0)),y(a.b,1.));}
frflpm(v11,c11)frflpm(v22,c22)frflpm(v33,c33)frflpm(v44,c44)

#define pMOD(d,e) d e(inout d a,d b){d c=floor((a/b)+.5);a=(fract((a/b)+.5)-.5)*b;return c;}
pMOD(vec1,pmod)pMOD(vec2,pmod)pMOD(vec3,pmod)pMOD(vec4,pmod)//repetitive only for less repetitive legacy namespace support.
vec1 pModMirror1(inout vec1 p,vec1 s){float c=pmod(p,s);p*=u2(mod(c,2.));return c;}
vec2 pModMirror1(inout vec2 p,vec2 s){vec2 c=pmod(p,s);p*=u2(mod(c,2.));return c;}
float pModInterval1(inout float p,float s,float b,float x){float c=pmod(p,s);if(c>x){p+=s*(c-x);c=x;}if(c<b){p+=s*(c-b);c=b;}return c;}
float pModSingle1(inout float p,float s){float c=floor((p/s)+.5);p=mix(p,(fract((p/s)+.5)-.5)*s,step(p,0.));return c;}
float pModPolar(inout vec2 p,float t
){vec2 q=c2p(p)
 ;float v=pi/t
 ;q.x+=v//offset by half-axis
 ;float c=floor(q.x*.5*v)
 ;q.x=mod(q.x,2.*v)-v
 ;p=p2c(q)
 ;if(abs(c)>=t*.5)c=abs(c)
 ;return c;}
vec2 pModGrid2(inout vec2 p,vec2 size
){vec2 c=floor((p+size*.5)/size)
 ;p=mod(p+size*.5,size)-size*.5;p*=mod(c,2.)*2.-vec2(1);p-=size*.5
 ;if(p.x>p.y)p.xy=p.yx;return floor(c*.5)
 ;}


const vec1 _1=.57735026919;
const vec4 PHI=vec4(1,sqrt(5.)*.5+vec3(-1,1,2)*.5);
const vec2 _B=norma(PHI.xy);
float Blob(vec3 p){p=ab(p);p=mx(p,p.yzx,st(p.x,ma(p.y,p.z)))
 ;vec1 l=le(p),b=ma(vec4(dot(p,vec3(_1)),dot(p.xz,norma(PHI.zx)),dot(p.yx,_B),dot(p.xz,_B)))
 ;return l-1.5-.15*cos(mi(sq(1.-b/l)*4.*pi,pi));}
//dist2plane [n]=plane Normal [d]shortestDistanceOfPlaneTo vec4(0) (see "hessian normal form")
#define fPlane(p,n,d) ad(dt(p,n),d)
#define abm(a,b) su(ab(a),b)//ditance taxicap   -b
#define lbm(a,b) su(le(a),b)//distance euclidean-b
#define boxf(a,b) ma(abm(a,b))
#define roundit(a) lbm(ma0(a),mi(ma0(-a)))//==(le(ma0(a))+ma(mi0(a)))
#define corner(a) roundit(mi0(a))//i am not convinced by this one
#define box(a,b) roundit(abm(a,b))
float fBox(vec3 p,vec3 b){vec3 d=abs(p)-b;return length(ma(d,vec3(0)))+ma(min(d,vec3(0)));}

#define cylinder(a,r,h) ma(lbm(a.xz,r),abm(a.y,h))//uv,radiusXZ,heightY;vertical cylinder
#define segment2(a,c) dd(su(a,mu(c,sat(di(dt(a,c),dd(c))))))//sqared orthogonal projection ,segment()sub ,is squared distance
#define segment(a,b,c) sq(segment2(su(a,b),su(c,b)))//pointAdistance to (diagonal) lineSegment from B to C
#define segmentY(a,b) mx(le(a.xz),le(vec3(a.xz,abm(a.y,b.x))),st(b.y,ab(a.y)))//pointAdistance to vertical lineSegment from b.x to b.y aling Ydomain???
#define torus(a,b) le(vec2(lbm(a.xz,b),a.y))//pointA distance of HOLLOW ring with radiusB ,lathe of [le(p.xz))-b]
#define disc2(p,l) mi(vec3(abs(p),le(vec2(p,l)),st(l,0.)))//disc()sub
#define disc(p,b) disc2(p.y,lbm(p.xz,b))//pointA distance of FILLED ring with radiusB ,lathe of [le(p.xz))-b],but filled!
#define hexCircum2(a,b) ma(vec2(ma(a.x*.866+a.z*.5,a.z),a.y)-b)//hexCircum()sub
#define hexCircum(a,b) hexCircum2(abs(a),b)//pointA.xyzDistance to hexagon,set by circumcircle radius b.x,with height b.x
float fCone(vec3 p,float r,float h//this needs some optimization;
){vec2 t,m,q=vec2(length(p.xz),p.y);t=q-vec2(0.,h);m=norma(vec2(h,r))
 ;float j=dt(t,vec2(m.y,-m.x))
 ;float d=max(dt(t,m),-q.y)
 ;if(q.y>h&&j<0.)d=max(d,le(t))
 ;if(q.x>r&&j>le(vec2(h,r)))d=max(d,le(q-vec2(r,0)))
 ;return d;}
//return distance of [p] measure distance to,[2 circle centers] and [2 sphere radii],connected by a tangential capped cone.
vec3 bicapsule3(vec3 p,vec3 a,vec3 b,float s,float t//https://www.shadertoy.com/view/4l2cRW
){vec3 c=b-a;float l=dot(p-a,c)/dd(c);vec3 u=mix(a,b,l)-p;//simple distance to line segment
 //Calculate the offset along segment according to the slope of the bicapsule
 ;c.x=length(c);float r=s-t;//find tangent angle for a point/sphere//the part in the tan()is the arcsecant function.
 ;float o=length(u)/tan(acos(1./(((c.x/abs(r))-1.)*c.x+1.)));//This is adjacent/tan(theta)-opposite
 ;o*=sign(r);//optional,for+1mult(),handle t>s as well
 ;l=sat(l-o);c=mix(a,b,l);return c+(normalize(p-c)*mix(s,t,l));}//And back to classic capsule closest point(with mix()ed radius)
vec1 bicapsule(vec3 p,vec3 a,vec3 b,float s,float t){float c=min(s,t);s-=c;t-=c
 ;return le(p-bicapsule3(p,a,b,s,t))-c+eRm;}//main problem here is that it is an UNSIGNED distance, so the smaller radius>eps.
 //must substract larger radius from smaller and add that as thickness for non-shitty normals
//similar to bicapsule,except that the straigt segment is a circle segment.
//for simplicity,the shape touches x=y at y=0 and at y=1;
//m.xy define 2 circle radii,both thicles touch the .y as describec above,for scale,and this is all that is needed to define the shape.
//return circle-circle-intersection.x;r.x=circle.left.radius;r.y=circle.right.radius;r.z=circles-centers.distance
float cci(vec3 r){float d=r.z*2.;r*=r;return(r.x-r.y+r.z)/d;}
//does not check for non-intersecticn cases! intersection.y is not important
//return signed distance of u to line trough m.xy and m.zw
float sd2l(vec2 u,vec4 m){
//vec2 d=m.xy;vec2 e=m.zw;//2 points to define a line.
//c.g=min(length(u-d),length(u-e))-.1;//draw 2 points
 vec2 f=m.zw-m.xy;f=vec2(-f.y,f.x);//calculate dorated differential
 return dot(u-m.xy,(f));//signed distance to line 
//more generally,f should be normalized here for proper caling.
//but scaling is irrelevant as we only care fror the sign 
}
#define  rac mix(.5,3.,sin(iTime)*.5+.5)
//earvageg is still far from standardized. rac is the central radius, should be parametric.
float EarVagEgg(vec2 u,vec2 m//m.x+m.xy<=1. is televant, larger values just return a circle.
){vec3 c=vec3(0)
 ;m=abs(m);m.xy=min(m.xy,vec2(.49999))//;m.xy=max(m.xy,vec2(-.49999))
 ;if(m.x>m.y)m.xy=m.yx//now it is worksave
 ;vec3 d=vec3(0,m.y,m.y),e=vec3(0,1.-m.x,m.x)//upper&lower circle ;.xy=center .z=radius  
 ,h=vec3(0,1.-m.x,rac-m.x),l=vec3(0,m.y,rac-m.y)//upper&lower intersect ring
 ;vec2 i=vec2(0);//center of 2 large circles,to be calculated by intersection
 ;i.y=m.y-cci(vec3(l.z,h.z,m.y+m.x-1.));//circle circle intersection.y
 ;float r=0.,y=m.y-i.y;i.x=-sqrt(l.z*l.z-y*y);//circle circle intersection.x
 ;u.x=abs(u.x)
 ;if (sd2l(u,vec4(i,d.xy))<.0)r=length(u-d.xy)-d.z//lower part//the conditional is likely better with a triconometric arcsecant thingie.
 ;else if(sd2l(u,vec4(i,e.xy))>.0)r=length(u-e.xy)-e.z//upper part//the conditional is likely better with a triconometric arcsecant thingie.
 ;else       r=length(u-i)-l.z-m.y//middle part
 ;return r;}//todo lathe this one //CylEarVagEgg() is capped cylinder to extrude it to 3d.
float CylEarVagEgg(vec3 u,vec2 m,float h
){float a=abs(EarVagEgg(u.xy,m))-.01
 ;a=abs(a-.05)-.02
 ;float b=abs(u.z)-h
 ;return ma(b,a);}
#define tiny .000001
vec2 gLLxX(vec2 A,vec2 B,vec2 C,vec2 D
){vec2 b=B-A,d=D-C,c=C-A
 ;float dotperp=b.x*d.y-b.y*d.x
 ;dotperp=max(abs(dotperp),tiny)*sign(dotperp)//jumps from -tiny to +tiny, lazy 0-avoidance, lines are never parallel!
 ;float t=(c.x*d.y-c.y*d.x)/dotperp
 ;return vec2(A.x+t*b.x,A.y+t*b.y);}//second life wiki geometry
//return distance to [wedge.rounded.circle],circle/IniniteLineSegment end at m.xy,the other line is horizontal.
float wedgeRound(vec2 u,vec2 m
){if(m.y==0.){if(m.x<0.)return u.y;if(u.x>m.x)return-abs(u.y);return-length(u-m);}//linear special cases
 ;m.y=abs(m.y)
 ;float l=length(m),d=dot(u,normalize(m)/l)-1.
 ;if(max(sign(u.x-l),sign(d))>0.)return(min(dot(vec2(-u.y,u.x),normalize(m)),u.y))//return minimum distance to 2 lines
 ;vec2 i=gLLxX(vec2(l,0),vec2(l,1),m,m+vec2(m.y,-m.x))//line line intersection
 ;return length(m-i)-length(u-i);}//aka ollj appolonean rounded railroad(bad c1 continuity)


float debugHg2(vec3 u){
 ;float r=0.
 ;float t=cos(iTime)*.4+.5
 ;//r=box(u,vec3(1))-t
 ;//r=length(u-bicapsule3(u,vec3(-.5,0,2),vec3(.5,0,2),.7,.2))-.1;
 ;r=CylEarVagEgg(u*.5,.5*iMouse.xy/iResolution.xy,.2)
 //bicapsule(u,vec3(1,0,0),vec3(0,0,1),.5,2.)
 ;//r=Blob(u)
 ;//r=fPlane(abs(u),vec3(1,1,1),-1.5)//the raymarcher is not ideal for this one,with its alpha fadeout.
 ;//r=boxf(u,vec3(1))-.5
 ;//r=corner(u.xy)
 ;//r=cylinder(u,1.,1.)
 ;//failed to make a rounded cylinder via roundit()
 ;//r=segment(u,vec3(0),vec3(1,2,3))-t
 ;//r=segmentY(u,vec2(1,2))//could not get it to work instantly
 ;//r=torus(u,1.)-1.
 ;//r=disc(u,vec2(1.))-2.//could not get it to work instantly
 ;//r=hexCircum(u,1.)-1.
 ;//r=fCone(u,1.,2.)
 ;return r;}
//2-distance input unions
//note,for higher dimensions,i may prefer; input (a,b); over; input (a.x,a.y)
#define miChamfer(a,b,r) mi(mi(a,b),(a-r+b)*sq(.5))
#define maChamfer(a,b,r) ma(ma(a,b),(a+r+b)*sq(.5))
#define meChamfer(a,b,r) maChamfer(a,-b,r)
//above chamfer are worse around 90deg; below chamfer is worse near 180deg. both are still good upper bounds. below is "smoother"
#define chamfer1802(a,b,r,e) mi(a,b)-(e)*(e)*.25/(r)
#define chamfer180(a,b,r) chamfer1802(a,b,r,ma0(r-abs(a-(b))))
//a is 2 distances,b is a radius //todo,compare with mima()
#define miRound(a,b) mi(ma0(a),-b)+le(ma0(b+a))//is likely wrong
#define maRound(a,b) -fOpRoundMin(-a,b)//is likely wrong
#define meRound(a,b) fOpRoundMin(a*vec2(1,-1),r);}//is likely wrong
/*
#define _M(S) (float a,float b,float r,float n){float c,m=min(a,b);if(a>r||b>r)return S*m;vec2 p=vec2(a,b);c=r*1.41421356237/(n*2.-0.58578643762);pR45(p);
float micolumns _M(1.)
 ;p.x+=sqrt(.5)*-r+c*sqrt(2.)
 ;if(mod(n,2.)==1.)p.y+=c;pmod(p.y,c*2.)
 ;p.y=mi(le(p)-c,p.x)
 ;return mi(mi(p.y,a),b)
 ;}

float macolumns _M(-1.)
 p.y+=c;p.x=sqrt(.5)*(r+c)/p.x
 ;if(mod(n,2.)==1.)p.y+=c;pmod(p.y,c*2.)
 ;p.y=-mi(le(p)-c,p.x)
 ;return mi(mi(p.y,a),b);}//fails on its own
#define mecolumns(a,b,r,n) -macolumns(a,-b,r,n)/**/


mat2 r2(float a){vec2 b=cs(a);return mat2(b.x,b.y,-b.y,b.x);}

// The [stairs] and [cpolumns] produce n-1 steps of a staircase/column:
float mistairs(float a,float b,float r,float n){float s=r/n;float u=b-r;return mi(mi(a,b),.5*(u+a+abs(mo(u-a+s,2.*s)-s)));}
#define mastairs(a,b,r,n)-mistairs(-a,-b,r,n)
#define mestairs(a,b,r,n)-mistairs(-a,b,r,n)
#define columns2(r,n,d) r/(n+d)
#define columns3 if(mod(n,2.)==1.)p.y+=t;a=pmod(p.y,t*2.);float s=length(p)-t
#define columns4 vec2 p=vec2(a,b);pR45(p)
// The "Columns" flavour makes n-1 circular columns at a 45 degree angle
//columns asserts that normlsd of a and b are at 90deg angle. it does not perform too well at other angles.
float micolumns(inout float a,float b,float r,float n){     float m=min(a,b);if(max(a,b)>=r*2.)return  m;float d=sqrt(.5);float t=columns2(r  ,n+2.,d);columns4;p.x-=(r-t)*d;columns3;return  min( min(s, p.x),m);}
float mecolumns(inout float a,float b,float r,float n){a=-a;float m=min(a,b);if(max(a,b)>=r   )return -m;float d=sqrt(.5);float t=columns2(r*d,n   ,d);columns4;p.x-=(r+t)*d;columns3;return -min(-min(s,-p.x),m);}
float mucolumns(inout float a,float b,float r,float n){return mecolumns(a,-b,r,n);}
#define mucolumns(a,b,r,n) mecolumns(a,-b,r,n)
float pipe(float a,float b,float r){return le(vec2(a,b))-r;}
//what,no pipeGroove?
#define engrave(a,b,r)  ma(a,mu((a+r-ab(b)),sq(.5)))
#define groove(a,b,r,h) ma(a,mi(ad(a,r),su(h,ab(b))))//r=depth h=width
#define tongue(a,b,r,h) -groove(-a,b,r,h)// mi(a,ma(su(a,r),su(ab(b),h)))
vec2 debugHg(vec3 u){
 ;return vec2(debugHg2(u),1)
 ;float r=0.
 ;u.z-=1.
 ;float t=cos(iTime)*.4+.5
 ;float a=box(u   ,vec3(1))-t
 ;//u.xy*=r2(1.)
 ;float b=box(u-1.,vec3(1))-.2
 ;//return mi(a,b)
 ;//return ma(a,b)
 ;//return ma(a,-b)
 ;//return miChamfer(a,b,0.5)//chamfer over more than the difference and get a nice roudned shape
 ;//return miChamfer(a,b,3.5)//chamfer over more than the difference and get a nice roudned shape
 ;//return maChamfer(a,b,.5)//not sure if broken or tracer issue
 ;//return meChamfer(a,b,.5)
 ;//return chamfer180(a,b,.5)//round chamfer is best
 ;//return -chamfer180(-a,-b,.5)//max() round chamfer is weird but fun
 ;//return -chamfer180(-a,b,.23)//max(a,-b) round chamfer easily gets discotinuous
 ;//return chamfer180(a,b,3.5)//overchamfering round is bumpy
 ;//return miRound(a,b)//todo fixme
 ;//return mistairs(a,b,.5,13.)
 ;//return mistairs(a,b,.5,13.)
 ;//return mestairs(a,b,.5,13.)
 ;a=micolumns(a,b,.5,4.)
 ;//return mecolumns(a,b,.5,4.)
 ;//return mucolumns(a,b,.5,4.) 
 ;//return macolumns(a,b,.5,4.)//macolumns fails
 ;//return mecolumns(a,b,.5,4.)
 ;//return pipe(a,b,.2)
 ;//return engrave(a,b,.5)
 ;//return -groove(-a,b,.5,.1)
 ;//return tongue(a,b,.5,.1)
 ;return vec2(a,1);}

//rgba colorspace matrices
#define ab012(a,b)(a+b*vec3(0,1,2))//desaturation.rgb kernel;b scales offset
//rainbow*()ro from purple to purple for range[0..1],this makes ab012()desaturate into semi-gaussian scattering.
vec3 rainbow(float a,float b){return u5cos(2.*pi*ab012(a,b));}//sine rainbow with offsets,desaturates colors for small b
vec3 rainbow2(float a,float b){return abs(u2(fract(ab012(a,b))));}//triangle rainbow with offsets,desaturates colors for small b
vec3 rainbow(float a){return rainbow(a,1./3.);}
vec3 rainbow2(float a){return rainbow2(a,1./3.);}
#define ToRgb(a) return c.z*mix(vec3(1.),sat(a(-c.x)),c.y);}
vec3 angleToColor(vec3 c){ToRgb(rainbow)//cos-mix
//vec3 hsv2rgb(vec3 c){ToRgb(rainbow2)//linear-mix not identical to the below,but close
vec3 hsv2rgb(const vec3 c){return c.z*mx(1.,sat(abs(fract(c.x+vec3(3,2,1)/3.)*6.-3.)-1.),c.y);}
vec3 rgb2hsv(vec3 a){vec4 K=vec4(0,-1,2,-3)/3.//https://www.shadertoy.com/view/MdGfWm
 ;vec4 P=mix(vec4(a.bg,K.wz),vec4(a.gb,K.xy),step(a.b,a.g));vec4 Q=mix(vec4(P.xyw,a.r),vec4(a.r,P.yzx),step(P.x,a.r))
 ;float D=Q.x-min(Q.w,Q.y),E=1e-10;return vec3(abs(Q.z+(Q.w-Q.y)/(6.*D+E)),D/(Q.x+E),Q.x);}
//vec3 HsvToRgb(vec3 c){vec3 p;p=abs(fract(c.xxx+vec3(3,2,1)/3.)*6.-3.);return c.z*mix(vec3(1),sat(p-1.),c.y);}




//general [Poeter-Duff] "Compositing Digital Images" siggraph 1984;
struct v21{vec2 a;vec1 b;};//currently only for alpha compositing plans
struct v31{vec3 a;vec1 b;};//...for MAT arithmetic
struct v41{vec4 a;vec1 b;};
v21 su(v21 a,v21 b){return v21(a.a-b.a,a.b-b.b);}
v21 mu(v21 a,v21 b){return v21(a.a*b.a,a.b*b.b);}
v21 mu(v21 a,vec1 b){return v21(a.a*b,a.b*b);}
v31 su(v31 a,v31 b){return v31(a.a-b.a,a.b-b.b);}
v31 mu(v31 a,v31 b){return v31(a.a*b.a,a.b*b.b);}
v31 mu(v31 a,vec1 b){return v31(a.a*b,a.b*b);}
v41 su(v41 a,v41 b){return v41(a.a-b.a,a.b-b.b);}
v41 mu(v41 a,v41 b){return v41(a.a*b.a,a.b*b.b);}
v41 mu(v41 a,vec1 b){return v41(a.a*b,a.b*b);}
//generalizing alpha-compositing functions,named after porterDuff
//https://en.wikipedia.org/wiki/Alpha_compositing
//https://doc.qt.io/archives/qq/qq17-compositionmodes.html
//.w=0 is fully transparent,.W=1 is fully visible
//iff(you want to keep an alpha channel after a composition) you must premultiplay all inputs with their alpha;
// a.xyz*=a.w ; b.xyz*=b.w
//.x inputs (ant interpolants) should be sat()ed,or you likely get [color inverted hazes],this version si still not haze-free?
//ommits the variant that returns 0 and a&b-inoput-swapped functions to half function/count
//ommits 2 functions,that return a or b,for simplicity
vec4 ut(vec4 a,vec1 b){return a*(1.-b);}v41 ut(v41 a,vec1 b){return mu(a,(1.-b));}
vec3 ut(vec3 a,vec1 b){return a*(1.-b);}v31 ut(v31 a,vec1 b){return mu(a,(1.-b));}
vec2 ut(vec2 a,vec1 b){return a*(1.-b);}v21 ut(v21 a,vec1 b){return mu(a,(1.-b));}
vec1 ut(vec1 a,vec1 b){return a*(1.-b);}v11 ut(v11 a,vec1 b){return mu(a,(1.-b));}//#define ut(a,b) (a*(1.-b))
#define Over 0.
#define Atop 1.
#define Out 2.
#define Xor 3.
#define In 4.
//note; colors atop of identical color is a too easy debugging culpit.
//note,that atop may returns the alpha of a,and not the alpha max(a,b)
vec4 pdOut(vec4 a,vec4 b){return ut(b,a.w);}
vec4 pdOver(vec4 a,vec4 b){return ut(b,a.w)+a;}
vec4 pdAtop(vec4 a,vec4 b){return ut(b,a.w)+a*b.w;}
vec4 pdXor(vec4 a,vec4 b){return ut(b,a.w)+ut(a,b.w);}
vec4 pdIn(vec4 a,vec4 b){return a*b.w;}//pdIn() is just multiplication,note swapped AB case here
//#define pd5(z)z pdOut(z a,z b){return ut(b,a.b);}z pdOver(z a,z b){return ad(ut(b,a.b),a);}z pdAtop(z a,z b){return ad(ut(b,a.b),mu(a,b.b));}z pdXor(z a,z b){return ad(ut(b,a.b),ut(a,b.b));}
//pd5(v11)pd5(v21)pd5(v31)pd5(v41)//if you want alpha seperated in a struct
//making tweening/unifying functionms of alpha compositing is silly fun. c is best range [0..1]
//this reduces it to 5. within pd() unifying function,because 4/5 include ut(a,b),i segregate 1/5_pdIn()
#define pdOverAtop(a,b,c) a*mix(b,1.,c)
#define pdOutXor(a,b,c) mix(ut(a,b),vec3(0),c)
vec3 pf(vec4 a,float c,vec4 b//c sets a mix type of this generalized function
){if(c>3.)return a.xyz*b.w//pdIn is just multiplication.
 ;vec3 d=vec3(0);d=mix(pdOverAtop(a.xyz,b.w,c),pdOutXor(a.xyz,b.w,c-2.),step(2.,c));return d+ut(b.xyz,a.w) ;}
vec4 pd(vec4 a,vec2 c,vec4 b){c.x=sat(c.x)//c sets a mix type.makes little sense to bilinn 4 functions on a plane,done anyways
 ;a=mix(a*mix(b.w,1.,c.y),ut(a,b.w)*c.y,c.x)+ut(b,a.w)
 ;return a;}//you likely want to a=sat(a) the c.x input,or have some negative-outlineglow-colors on -1>c>1



//smin: 2nd letter sets 1of3 boolean fuzzy-unions,"mex()"==max(a,-b) is semi-nonsense;
//3rd letter sets type of smoothing union
//sMinExponential [m*e] is slow but commutative (like multiple parallel resistors)
//you should pre-reciprocal the [k] of [m*e]
///mie(a,b,k)=(log(exp(k*-a)+exp(k*-b))/k);} is expomemtial smooth minimum; mee()mes()mer() are [boolean nonimplication,0100]
//vec4 mu(vec3 a,vec4 b){return a*b;}
#define mae(a,b,k) di(ln(ad(ex(mu(k,a)),ex(mu(k,b)))),k)//sMaxExponential
#define mee(a,b,k) ne(mae(a,ne(b),k))//sDifExponential
#define mie(a,b,k) mee(ne(a),b,k)//sMinExponential
//
//w11 mx(w11 a,w11 b,vec1 c){return ad(mu(c,su(a,b)),a);} //mix(a,b,c) [~=] c*(a-b)+a;<- actually false
w11 mx(w11 a,w11 b,w11 c){return w11(mix(a.a,b.a,c.a),mix(a.b,b.b,c.a)//close enough, likely false
//ad(mu(c.b,su(a.b,b.b)),a.b)
//mix(a.b,b.b,c.b)
);}//somehow almost correct.
//SminPPolynomial [m*e] by IQ is fast but not commutative; mis2() is subroutine
#define mis2(a,b,k,h) su(mx(b,a,h),mu(mu(k,h),su(1.,h)))
//#define mis2(a,b,k,h) (mix(b,a,h)-(k)*(h)*(1.-(h)))
//#define mis(a,b,k) mis2(a,b,k,sat(u5(((b)-(a))/(k))))
#define mis(a,b,k) mis2(a,b,k,sat(ad(mu(di(su(b,a),k),.5),.5)))
#define mas(a,b,k) ne(mis(ne(a),ne(b),k))
#define mes(a,b,k) ne(mis(   a ,ne(b),k))
//sMinQuadratic mar()mir()mer() use the subroutine mima(),that unifies (smoothened by ar(k)) quadratic min()+max()+mex().
//soft logic,by @paniq?   absurdly small epsilon evades a division by 0
vec2 ar(float r){return vec2(r,mix(.5/(r+1e-15),0.,step(r,0.)));}//apolonean round union
//uncaught use-case; max(-a,b),gets negated,which is just done by swapping a and b instead.
//s.z,s.w:radius&scaling,as returned by ar() k is vec4() for the option of interpolating between multiple [k]
#define mima4(a,b,k,d) (.5*(a+k.x*(b+k.w*d*d)))
#define mima3(a,b,k) mima4(a,b,k,ma0(k.z-b))
#define mima2(a,b,k) mima3(a+b,abs(a-(b)),k)
#define mima(a,b,k) mima2(a,(b)*k.y,k)
/* //use case examples that specialize the general function mima()
float ma(float a,float b){return mima(a,b,vec4(1,1,0,0));}//max(a,b)
float mi(float a,float b){return mima(a,b,vec4(-1,1,0,0));}//min(a,b)
float me(float a,float b){return mima(a,b,vec4(1,-1,0,0));}//max(a,-b)
//k is a circleRadius,[m*r] ignores negative k because it does k.z*k.z*k.w*/
float mar(float a,float b,float k){return mima(a,b,vec4(1,1,ar(k)));} // max( a,b,k)=-min(-a,-b,k)
float mir(float a,float b,float k){return mima(a,b,vec4(-1,1,ar(k)));}//-max(-a,-b,k)= min( a,b,k)
float mer(float a,float b,float k){return mima(a,b,vec4(1,-1,ar(k)));}// max( a,-b,k)=-min(-a,b,k)

vec4 demoSmin(vec2 u,vec2 m,vec2 n//smin (exponential and polinomial) , with automativDifferenciation
){w11 a=w11(u.x-m.x*9.,1.)
 ;w11 b=w11(u.x-m.x*9.,1.)
 ;m.x=abs(m.x)+.2//optional evasion of very short wavelength
 ;vec4 c=vec4(0)
 ;float amp=1.//m.y
 ;a=   mu(co(di(a,di(m.x,mu(pi,.5)))),amp)//a.b is analytic first derivative of a.a
 ;b=ad(mu(si(di(b,di(m.x,mu(pi,.5)))),amp),mu(si(b),.3))//b.b is analytic first derivative of b.a
 ;a=su(co(mu(a,2.)),mu(a,.2))
 ;b=su(si(mu(b,2.)),mu(b,.561))
 ;w11 d=mae(a,b,1./m.y)
 ;w11 e=mae(a,b,1./m.y)
 ;w11 f=mas(a,b,3.*m.y)
 ;c.y=abs(a.a-u.y)/sqrt(1.+a.b*a.b)//euclidean_scale by first derivative. (bad near very thin extrema)
 ;c.x=abs(b.a-u.y)/sqrt(1.+b.b*b.b)//euclidean_scale by first derivative. (bad near very thin extrema)
 ;c.z=abs(d.a-u.y)/sqrt(1.+d.b*d.b)//euclidean_scale by first derivative. (bad near very thin extrema)
 ;c.w=abs(f.a-u.y)/sqrt(1.+f.b*f.b)//euclidean_scale by first derivative. (bad near very thin extrema)
 ;float aaa=4./min(iResolution.x,iResolution.y)
 ;//c=abs(c-.2)-.1//optional shows how this is NOT distance2Sin()
 ;c=abs(c-aaa*2.)-aaa*.5//optional double line
 ;c=smoothstep(aaa,-aaa,c)
 ;vec4 rainb=fract((vec4(0,1,2,3)+11.)*phi)
 ;vec4 x=vec4(angleToColor(vec3(rainb.x,1,1)),c.x)//first derivative
 ;vec4 y=vec4(angleToColor(vec3(rainb.y,1,1)),c.y)//smoothened graph  in yellow
 ;vec4 z=vec4(angleToColor(vec3(rainb.z,1,1)),c.z)//coordinate system in magenta/pink
 ;vec4 w=vec4(angleToColor(vec3(rainb.w,1,1)),c.w)//coordinate system in magenta/pink
 ;x.xyz*=x.w;y.xyz*=y.w;z.xyz*=z.w;w.xyz*=w.w//alpha premultiplied
 ;x=pdOver(y,x)
 ;x=pdOver(z,x)
 ;x=pdOver(w,x)
 ;return x;}

float miy(float a,float b,float r//https://www.shadertoy.com/view/XtccDr
){float e=max(r*.02,(abs(a-b)/r));return min(a,b)-max(.01,(r*e*.75*(exp(1.-(e*2.5))))*.5/max(a,b));}
// Commutative smooth minimum function.Provided by Tomkh,from Alex Evans's (aka Statix)talk:
float mia(float a,float b,float k){float f=ma0(1.-abs(b-a)/k);return min(a,b)-k*.25*f*f;}

#define smoothBump(x,y,z,w) smoothstep(x-z,x+z,w)*smoothstep(y+z,y-z,w)








// "Generalized Distance Functions" by Akleman and Chen ,often included in hg_sdf
// see the Paper at https://www.viz.tamu.edu/faculty/ergun/research/implicitmodeling/papers/sm99.pdf
// This set of constants is used to construct a large variety of geometric primitives.
// Indices are shifted by 1 compared to the paper because we start counting at Zero.
// Some of those are slow whenever a driver decides to not unroll the loop,
// which seems to happen for fIcosahedron und fTruncatedIcosahedron on nvidia 350.12 at least.
// Specialized implementations can well be faster in all cases.
// Macro based version for GLSL 1.2/ES 2.0 by Tom

#define GDFVector0 vec3(1,0,0)
#define GDFVector1 vec3(0,1,0)
#define GDFVector2 vec3(0,0,1)
#define GDFVector3 norma(vec3(1,1,1))
#define GDFVector4 norma(vec3(-1,1,1))
#define GDFVector5 norma(vec3(1,-1,1))
#define GDFVector6 norma(vec3(1,1,-1))
#define GDFVector7 norma(vec3(0,1,PHI+1.))
#define GDFVector8 norma(vec3(0,-1,PHI+1.))
#define GDFVector9 norma(vec3(PHI+1.,0,1))
#define GDFVector10 norma(vec3(-PHI-1.,0,1))
#define GDFVector11 norma(vec3(1,PHI+1.,0))
#define GDFVector12 norma(vec3(-1,PHI+1.,0))
#define GDFVector13 norma(vec3(0,PHI,1))
#define GDFVector14 norma(vec3(0,-PHI,1))
#define GDFVector15 norma(vec3(1,0,PHI))
#define GDFVector16 norma(vec3(-1,0,PHI))
#define GDFVector17 norma(vec3(PHI,1,0))
#define GDFVector18 norma(vec3(-PHI,1,0))
#define fGDFBegin float d=0.;
// Version with variable exponent.
// This is slow and does not produce correct distances,but allows for bulging of objects.
#define fGDFExp(v) d+=pow(abs(dot(p,v)),e);
// Version with without exponent,creates objects with sharp edges and flat faces
#define fGDF(v) d=max(d,abs(dot(p,v)));
#define fGDFExpEnd return pow(d,1./e)-r;
#define fGDFEnd return d-r;
// Primitives follow:
float fOctahedron(vec3 p,float r,float e
){fGDFBegin
 fGDFExp(GDFVector3) 
 fGDFExp(GDFVector4) 
 fGDFExp(GDFVector5) 
 fGDFExp(GDFVector6)
 fGDFExpEnd}
 
/*
float fDodecahedron(vec3 p,float r,float e){
 fGDFBegin
 ;d+=pow(abs(dot(p,norma(vec3(0,PHI,1)))),e);
 //fGDFExp(GDFVector13)
 fGDFExp(GDFVector14)
 fGDFExp(GDFVector15)
 fGDFExp(GDFVector16)
 fGDFExp(GDFVector17)
 fGDFExp(GDFVector18)
 fGDFExpEnd}
 /*
float fIcosahedron(vec3 p,float r,float e
){fGDFBegin
 fGDFExp(GDFVector3) fGDFExp(GDFVector4) fGDFExp(GDFVector5) fGDFExp(GDFVector6)
 fGDFExp(GDFVector7) fGDFExp(GDFVector8) fGDFExp(GDFVector9) fGDFExp(GDFVector10)
 fGDFExp(GDFVector11) fGDFExp(GDFVector12)
 fGDFExpEnd}
float fTruncatedOctahedron(vec3 p,float r,float e
){fGDFBegin
 fGDFExp(GDFVector0) fGDFExp(GDFVector1) fGDFExp(GDFVector2) fGDFExp(GDFVector3)
 fGDFExp(GDFVector4) fGDFExp(GDFVector5) fGDFExp(GDFVector6)
 fGDFExpEnd}
float fTruncatedIcosahedron(vec3 p,float r,float e
){fGDFBegin
 fGDFExp(GDFVector3) fGDFExp(GDFVector4) fGDFExp(GDFVector5) fGDFExp(GDFVector6)
 fGDFExp(GDFVector7) fGDFExp(GDFVector8) fGDFExp(GDFVector9) fGDFExp(GDFVector10)
 fGDFExp(GDFVector11) fGDFExp(GDFVector12) fGDFExp(GDFVector13) fGDFExp(GDFVector14)
 fGDFExp(GDFVector15) fGDFExp(GDFVector16) fGDFExp(GDFVector17) fGDFExp(GDFVector18)
 fGDFExpEnd}
float fOctahedron(vec3 p,float r
){fGDFBegin
 fGDF(GDFVector3) fGDF(GDFVector4) fGDF(GDFVector5) fGDF(GDFVector6)
 fGDFEnd}
float fDodecahedron(vec3 p,float r
){fGDFBegin
 fGDF(GDFVector13) fGDF(GDFVector14) fGDF(GDFVector15) fGDF(GDFVector16)
 fGDF(GDFVector17) fGDF(GDFVector18)
 fGDFEnd}
float fIcosahedron(vec3 p,float r
){fGDFBegin
 fGDF(GDFVector3) fGDF(GDFVector4) fGDF(GDFVector5) fGDF(GDFVector6)
 fGDF(GDFVector7) fGDF(GDFVector8) fGDF(GDFVector9) fGDF(GDFVector10)
 fGDF(GDFVector11) fGDF(GDFVector12)
 fGDFEnd}
float fTruncatedOctahedron(vec3 p,float r
){fGDFBegin
 fGDF(GDFVector0) fGDF(GDFVector1) fGDF(GDFVector2) fGDF(GDFVector3)
 fGDF(GDFVector4) fGDF(GDFVector5) fGDF(GDFVector6)
 fGDFEnd}
float fTruncatedIcosahedron(vec3 p,float r
){fGDFBegin
 fGDF(GDFVector3) fGDF(GDFVector4) fGDF(GDFVector5) fGDF(GDFVector6)
 fGDF(GDFVector7) fGDF(GDFVector8) fGDF(GDFVector9) fGDF(GDFVector10)
 fGDF(GDFVector11) fGDF(GDFVector12) fGDF(GDFVector13) fGDF(GDFVector14)
 fGDF(GDFVector15) fGDF(GDFVector16) fGDF(GDFVector17) fGDF(GDFVector18)
 fGDFEnd}

*/



//someone overthought checkerboard patterns
//float xnor(float x,float y){return abs(x+y-1.);}// abs(0+0-1)-1 abs(1+0-1)-0 abs(0+1-1)-0 abs(1+1-1)-1
//xnor(xnor(e.x,e.y),e.z)//checkerboard3d












//ortographics
mat4 rotX4(float a){vec2 r=cs(a);return mat4(r.x,0,r.y,0,0,1  ,0  ,0,-r.y,0   ,r.x,0,0,0,0,1);}
mat4 rotY4(float a){vec2 r=cs(a);return mat4(1  ,0,0  ,0,0,r.x,r.y,0,0   ,-r.y,r.x,0,0,0,0,1);}
mat3 rotX3(float a){return mat3(rotX4(a));}
mat3 rotY3(float a){return mat3(rotY4(a));}
//cam prjections
v33 cam(vec2 u
){
  ;vec4 mouse=  iMouseZwFix(iMouse,true)
 ;float camOrbit=5.//camera orbits at distance to vec3(0)
 ;//return v33(vec3(0,0,-camOrbit),norma(vec3(u,1)))//super lazy alternative
 ;vec2 m=-.03*(mouse.xy-iResolution.xy*.5)//mouse input
 ;vec2 n=cs(vec2(1,.61)*iTime)//autopilot
 ;m=mx(n,m,step(0.,mouse.z))
 ;mat3 a=rotX3(m.x)*rotY3(m.y)
 ;vec3 pos=a*vec3(0,0,-camOrbit)
 ;vec3 dir=norma(a*vec3(u,1))
 ;return v33(pos,dir);}



//Phong+debugPlanes
//
//https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection#Algebraic_form
//https://www.shadertoy.com/view/llXcDr
#define tracePlane(po,pd,o,d) dot(po-(o),pd)/dot(d,pd)
//traceR if(t<0) return -1; return t;
#define traceR(t) mix(-1.,t,step(t,0.))
#define raytrace_plane(po,pd,o,d) traceR(tracePlane2(po,pd,o,d))
vec1 planeDebug2(vec3 o,vec3 d,float c,inout float t,vec2 n,const int f//ray,ray,curPlane,time,domainLengths
){if(n.x<c&&n.y>0.//debug plane //n.x=o.y n.y=d.y
 ){vec3 a=(mat3(1)*c)[1]
 //above line needs gl1.0 0:1318: '[]' : Index expression must be constant//
  ;float p=tracePlane(a,a*t,o,d)
  //;if(abs(p)<9.){
   ;p=df(o+d*p)
   ;float q=fract(p*8.)/(p*p*p*p+4.)
   ;p=mix(q,mix(.3,.7,q),step(p,0.))
   ;return p
   ;}//}
 ;return 0.;}
//x can be y,beware that z may be hidden from some cameras,need to rotate camera asound.
//d is linked to e; e=x f=0 ; e=y f=1 ; e=z f=2 (; e=w f=3)
#define planeDebug(a,b,c,d,e,f) planeDebug2(a,b,c,d,vec2(a.e,b.e),f)

vec4 DebugPlanes(vec3 o,vec3 d,float t
){float c0=sin(iTime*.5)*2.
 ;vec4 a=planeDebug(o,d,c0,t,x,0)*vec4(0,1,.5,1)
 ;vec4 b=planeDebug(o,d,c0,t,y,1)*vec4(.5,0,1,1)
 ;vec4 c=planeDebug(o,d,c0,t,z,2)*vec4(1,.5,0,1)
 ;return sat((a+b+c)*2.);}
vec3 tex(vec3 u//textureid is 2 integers, .x one sets hue, .y sets saturation.
){return cellular(u*5.,iTime).xyx
 //;return fract(u*4.+cos(iTime))
 ;}


vec2 pModMirror2(inout vec2 p,vec2 size//likely duped
){vec2 halfsize=size*.5;
 vec2 c=floor((p+halfsize)/size);
 p=mod(p+halfsize,size)-halfsize;
 p *=mod(c,vec2(2))*2.-vec2(1);
 return c;}
 
//these likely have simpler expressions,its too late timeofday for me to bother now.
vec3 dt(vec3 a,mat3 b){return vec3(dot(a,b[0]),dot(a,b[1]),dot(a,b[2]));}
mat3 mu3(vec3 a,mat3 b){return mat3(a.x*b[0],a.y*b[1],a.z*b[2]);}
vec3 suv(mat3 a){return a[0]+a[1]+a[2];}


//uvw==*3 boolean active mirrors' in the coxeter diagram
////https://www.shadertoy.com/view/MltSD4
vec2 poly(vec3 p,float type,vec3 uvw//knighty's fold-n-cut polyhedra
){vec2 o=iMouse.xy/iResolution.xy //;p*=o.x;uvw*=o.x/nope no easy scaling
 ;o=vec2(0,u5(cos(iTime*.61))*.1)
 ;const vec2 tetra=vec2(.5,sqrt(.5))
 ;vec2 m=vec2(.80901699,.30901699)//docecahedral
 ;if(type<2.){m=vec2(.5,sqrt(.5));m=mix(tetra,tetra.yx,step(0.,type));}//tetrahedral/octahedral
 ;vec3 c=vec3(-.5,-m.x,m.y)
 ;float id=1.;//will store a lot of signs
 ;for(int i=0;i<5;i++){id*=4.;id+=sign(p.x)+sign(p.y)*2.;p.xy=abs(p.xy);p-=2.*mi0(dot(p,c))*c;}
 //;id*=4.;id+=sign(p.x)+sign(p.y)*2.;
 ;mat3 y=mat3(0,0,1,m.y,0,.5,0,m.yx)
 ;p-=norma(suv(mu3(uvw,y)))
 ;y[1]=norma(y[1])
 ;y[2]=norma(y[2])
 ;vec3 z=vec3(dd(p-vec3(mi0(p.x),0,0))
             ,dd(p-vec3(0,mi0(p.y),0))
             ,dd(p-mi0(dot(p,c))*c));
 ;return vec2(mi(ma(dt(p,y))-o.x*3.,sq(mi(z))-o.y),id)//-.05 is a rounded bloney corner
 //the last value later sets saturation(or hue)
 ;}

vec3 dfPoly(vec3 p
){vec3 modp=floor(p*.25)
 ;p-=2.+4.*modp
 ;//made up hash of position to an integer 0-15 to yield 16 different polyhedra
 ;float index=mod(5.*modp.x+7.*modp.y+13.*modp.z,16.)
 ;float modindex=mod(index,7.)
 ;//choosing the 'active mirrors' in the coxeter diagram,can be 1,2 or all 3
 ;float a=(mod(modindex,2.)==1.|| modindex==6.)?1.:0.
 ;float b=(modindex<4.)?1.:0.//sign(modindex-4.)//
 ;float c=(modindex>=2.&&modindex<6.)?1.:0.
 ;//there are 7 unique shapes with octahedral symmetry,7 with dodecahedral,2 remaining with tetrahedral 
 ;float d=index<14.?2.:0.
 ;float type=index<7.?1.:d
 ;vec3 pol=vec3(poly(p,type,vec3(a,b,c)),index/15.).xzy//last value later sets hue(or saturation)
 ;return pol;}


float df(vec3 p){
#if 0
 ;p.xz=-p.xz;
 ;vec2 q=pModMirror2(p.xz,vec2(4.5))//repetition
#endif
 ;//return dfPoly(p).x//array of 16 different spheroid
 ;return debugHg(p).x//hg_sdf debug playgound
 //;float dodec=fDodecahedron(p-vec3(-9,2,-4)/4.,.7)//i like how this is placed n the mirroring plane
 ;float b=fBox(p-vec3(0,-.1,0),vec3(1))
 ;float s=length(p-vec3(1.+sin(iTime*.25)*.2,.8,1))-1.//sphere
 ;s=min(b,s)
 //;return s;
 ;float d=mistairs(b,s,.7,4.)
 ;p.xz*=r1(iTime*.041);p.zy*=r1(iTime*.021)//lazy small rotation
 ;vec2 n=cellular(p*5.61,1./7.)
 ;//d-=(n.y-n.x)*.05//too much foreach march iteration.
 ;return d;}
 //;return min(d,dodec);}

vec4 Phong(vec3 d,vec3 l,vec4 u){//direction,lightDirection,uvHit
 ;vec3 n=dNormal(u.xyz)
 ;float diffuse=max(0.,dot(n,l))
 ;float spec=max(0.,dot(reflect(l,n),norma(d)))
 ;spec=pow(spec,16.)*.5
 ;vec2 tid=dfPoly(u.xyz).yz//surfaceID is distanceField specific
 ;tid=fract(tid*phi)//golden ration hash is most uniform but not very "blue".
 ;tid.y=u5(tid.y)//more saturation
 ;//vec3 surf=angleToColor(vec3(tid,1))//surfaceID to color
 ;//surf=mix(tex(u.xyz),surf),.5)//mix in a whatever 3d texture we have.
 ;vec3 surf=tex(u.xyz);
    //;vec4 rainb=fract((vec4(0,1,2,3)+11.)*phi)
    //;vec4 x=vec4(angleToColor(vec3(rainb.x,1,1)),c.x)//first derivative
 ;vec3 c=mix(vec3(0,.1,.3),vec3(1,1,.9),diffuse)*surf+spec*vec3(1,1,.9)
 //not i got to make a difference between distance fog and distance fadeout
 ;float alp=pow(dd(u.xyz),1.)
 ;alp=sat(alp)
 ;return vec4(c,alp)//distance fadeout
 //;return vec4(c,eul-log(length(u.xyz)))//silly fog
 ;}
vec4 shade(vec3 ray_start,vec3 ray_dir,vec3 lir,vec4 hit
){float ray_len
 ;vec3 dir=hit.xyz-ray_start
 ;vec4 c=DebugPlanes(ray_start,ray_dir,length(dir))
 ;if(hit.w==0.)return c
 ;vec4 p=Phong(dir,lir,hit)
 ;return vec4(mix(p.xyz,c.xyz,.5),1.)
 ;}




vec4 demo2NoiseCel(vec2 u,vec2 m
){vec3 o=vec3(u*6.,cos(iTime)*4.)
 ;o.xz*=r1(iTime*.041);o.zy*=r1(iTime*.021)//lazy small rotation
 ;return vec4(cellular(o,iTime*.161),0,1);}

vec4 demoHg3d(vec2 u
){vec3 lir=norma(vec3(.5,1.0,-.25))
 ;v33 ray=cam(u)
 ;//v12 dt=trace(ray.a,ray.b)//td.a is distanceToCamera dt.b is a 2d textureId
 ;vec4 c=shade(ray.a,ray.b,lir,trace(ray.a,ray.b))
 ;//c.xyz=pow(c.xyz,vec3(.44))//gamma
 ;return c;}

vec4 demoAd2d(vec2 u,vec2 m//deomes 1d automatic differentiation
){vec3 c=vec3(0)
 ;w11 a=w11(u.x*9.,1.)
 ;a=w11(u.x,1.)
 ;a=si(ex((a)))//a.b is analytic first derivative of a.a
 //;a=(si(fr(a)))
 ;c.x=abs(a.a-u.y)/sqrt(1.+a.b*a.b)//euclidean_scale by first derivative. (bad near very thin extrema)
 ;float o=c.x
 ;c.y=a.b
 ;c.yz-=u.y
 ;c.yz=abs(c.yz)
 ;float aaa=4./min(iResolution.x,iResolution.y)
 ;c=smoothstep(aaa,-aaa,c-aaa)
 ;if(abs(a.b)<aaa*50.)c.z=max(c.z,pow(1.-abs(a.b)*2.,4.))//mark local extrema with vertical lines (better line width would need 2nd derivative)
 ;c=c.zyx//;c=mix(c,c.yzx,iMouse.x/iResolution.x)*2.#
 ;vec4 x=vec4(0  ,c.x,c.x,c.x)//first derivative
 ;vec4 y=vec4(c.y,0  ,c.y,c.y)//smoothened graph  in yellow
 ;vec4 z=vec4(c.z,c.z,0  ,c.z)//coordinate system in magenta/pink
 ;x=pdOver(y,x)//2 graphs,one is 1st derivative  in cyan/blue
 ;x=pdOver(z,x)//add coordinate system that highlights loccal extrema
 ;return x;}

vec4 demo2d1(vec2 u,vec2 m,vec2 n
){vec4 c=vec4(0)
 ;c.x=dd(m-u);c.y=dd(n-u);c.w=dd(mix(n,m,abs(cos(iTime*2.)))-u)//abs(cos())bounce fopr direction
 ;c.xyw=sqrt(c.xyw)
 ;u=abs(u)
 ;c.z=ma(u)
 ;float thick=.005,diam=.1
 ;c=smoothstep(.01,-.01,abs(abs(c-diam)-diam+thick)-thick)//i know theres better ways to do this abs(abs())identities...
 ;c.xy+=c.w;return vec4(c.xyz,ma(c.xyz))
 ;vec4 a=pdOver(vec4(0,c.y,0,c.y),vec4(c.x,0,0,c.x))//green over red
 ;c     =pdOver(vec4(c.w,c.w,0,c.w),vec4(0,0,c.z,c.z))//yellow over blue
 ;c     =pdOver(a,c)//green over red over yellow over blue
 ;return c;}

vec3 ss(float a,vec3 b){return smoothstep(a,-a,b);}

vec4 pd(vec2 u
){vec2 m=fra(iMouse.xy)
 ;if(iMouse.z<.0)m=-vec2(.5)//while(mouse up)simulate mouse down.
 ;vec2 s=-vec2(1.,sin(iTime))//vector moves over time.
 ;vec3 e=vec3(dd(u)
 ,dd(u-m)
 ,dd(u-s))//eucliden distance projection.
 ;e=sqrt(e)//delayed square root
 ;e-=.5//circle radius
 ;e=abs(e)-.2;//turn cirlce into ring
 ;float SSAA=(cos(iTime)+1.25)*12./min(iResolution.x,iResolution.y);//screen-space-anti-aliasing
 ;e=ss(SSAA,e)
 ;e=sat(e)
 ;vec3 g=vec3(.96,.25,.05)//color ramp equals cheap colorblind mode.
 ;vec4 c0=vec4(g,e.x)
 ;vec4 c1=vec4(g.yzx,e.y)
 ;vec4 c2=vec4(g.zxy,e.z)//some colors with e.rgb as alpha channel.
 ;c0.rgb*=c0.w//general form scales .rgb by alpha,
 ;c1.rgb*=c1.w//...this acoids some division by 0 cases
 ;c2.rgb*=c2.w
 ;vec4 O=pdOver(c0,c1);O=pdOver(O,c2)
 ;//O=sXor(c0,c1);O=sXor(O,c2)
 ;//O=sAtop(c0,c1);O=sAtop(O,c2)
 ;vec4 bg=vec4(vec3((checkerBool2(u))),u.y)
 //;bg.rgb=pow(bg.rgb,vec3(2.2))
 ;if(O.w!=0.)O.rgb/=O.w
 ;O=pdOver(O,bg);
 ;O.rgb=pow(O.rgb,vec3(1./2.2))/**/
 ;return O;}

void mainImage(out vec4 O,vec2 u
){u=fra(u)
 ;vec4 mouse=iMouseZwFix(iMouse,true)
 ;vec2 m=fra(mouse.xy)
 ;vec2 n=fra(mouse.zw)
 ;O=vec4(0)
 ;O=pdOver(O,demo2d1(u,m,n))//mouseCoords
 ;//O=pdOver(O,demoAd2d(u,m))//ad 1d
 ;O=pdOver(O,demoSmin(u,m,n))//ad 1d smin
 ;O=pdOver(O,demoHg3d(u/2.))//hg_sdf,u-scaling sets camera distance to center
 ;O=pdOver(O,demoComplex(u,m,n))
 ;//O=pdOver(O,demo2NoiseCel(u,m)*vec4(.3,.3,.3,1.))//celularNoise
 ;O=pdOver(O,vec4(vec3((checkerBoolT(u*1.61))*.5+.25),u.y))//checkerboard background
 ;}







/* //hg_sdf namespace legacy compatibility:
#define fBoxCheap(p,b) boxf(p,b)
#define fBox2Cheap(p,b) boxf(p,b)
#define fBox(a,b) box(a,b)
#define fBox2(a,b) box(a,b)
#define fCorner(a) corner(a)
#define fCylinder(a,r,h) cylinder(a,r,h)
#define dSegment(a,b,c) segment(a,b,c)
float fCapsule(vec3 p,vec1 r,vec3 b){return segmentY(p,b)-r;}
//float fCapsule(vec3 a,vec3 b,vec3 c,vec1 r){return segment(a,b,c)-r;}//parser error?
#define fTorus(a,i,b) (torus(a,b)-(i))
#define circle(a,b) torus(a,b)
#define fDisc(a,b) disc(a,b)
#define fHexagonCircumcircle(a,b) hexCircum(a,b)
#define fHexagonIncircle(a,b) hexCircum(a,vec2(b.x*.866,b.y))
#define fOpUnionChamfer(a,b,r)        miChamfer(a,b,r)
#define fOpIntersectionChamfer(a,b,r) maChamfer(a,b,r)
#define fOpDifferenceChamfer(a,b,r)   meChamfer(a,b,r)
#define fOpUnionSoft(a,b,r) chamfer180(a,b,r)
#define fOpUnionRound(a,r)        miRound(a,r)
#define fOpIntersectionRound(a,r) maRound(a,b)
#define fOpDifferenceRound(a,b)   meRound(a,b)
#define fOpRoundE(a,b)            meRound(-a,b)
#define fOpUnionStairs(a,r,n)        miStairs(a,r,n)
#define fOpIntersectionStairs(a,r,n) maStairs(a,r,n)
#define fOpDifferenceStairs(a,b,r,n) meStairs(a,r,n)
#define fOpPipe(a,b,r) pipe(a,b,r)
#define fOpEngrave(a,b,r) engrave(a,b,r)
#define fOpGroove(a,b,r,h) groove(a,b,r,h)
pMOD(vec1,pMod1)pMOD(vec2,pMod2)pMOD(vec3,pMod3)pMOD(vec4,pMod4)/**/


