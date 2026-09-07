void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 p=floor(fract(fragCoord));bvec2 e=equal(p,vec2(0.0));bvec2 n=notEqual(p,vec2(1.0));fragColor=vec4(e.x?1.0:0.0,e.y?1.0:0.0,n.x?1.0:0.0,1.0);}
