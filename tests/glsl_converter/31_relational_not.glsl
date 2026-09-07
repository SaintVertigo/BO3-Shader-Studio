void mainImage(out vec4 fragColor,in vec2 fragCoord){bvec2 a=greaterThan(fragCoord/iResolution.xy,vec2(0.5));bvec2 b=not(a);fragColor=vec4(b.x?1.0:0.0,b.y?1.0:0.0,0.0,1.0);}
