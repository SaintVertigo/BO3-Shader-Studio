void mainImage(out vec4 fragColor,in vec2 fragCoord){mat2 m=mat2(0.0,-1.0,1.0,0.0); vec2 v=m*vec2(1.0,0.0); vec2 q=v*m; fragColor=vec4(q,0.0,1.0);}
