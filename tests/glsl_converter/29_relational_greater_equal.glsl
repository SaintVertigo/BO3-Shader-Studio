void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 p=fragCoord/iResolution.xy;bvec2 b=greaterThanEqual(p,vec2(0.5));fragColor=vec4(b.x?1.0:0.0,b.y?1.0:0.0,0.0,1.0);}
