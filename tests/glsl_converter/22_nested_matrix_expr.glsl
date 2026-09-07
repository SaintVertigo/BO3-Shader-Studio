mat2 r(float a){return mat2(cos(a),-sin(a),sin(a),cos(a));} void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 p=r(0.5)*(r(0.25)*vec2(1.0,0.0));fragColor=vec4(p,0.0,1.0);}
