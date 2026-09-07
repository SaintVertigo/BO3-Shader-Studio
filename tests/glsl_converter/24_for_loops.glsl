void mainImage(out vec4 fragColor,in vec2 fragCoord){float v=0.0;for(int i=-2;i<=2;i++){v+=sin(float(i)+iTime);}fragColor=vec4(vec3(v*0.1+0.5),1.0);}
