void mainImage(out vec4 fragColor,in vec2 fragCoord){ivec2 a=ivec2(2);uvec3 b=uvec3(3u);bvec2 c=bvec2(true);fragColor=vec4(float(a.x)/2.0,float(b.y)/3.0,c.x?1.0:0.0,1.0);}
