void makeColor(vec2 p,out vec3 c){if(p.x>0.5)c=vec3(1.0,0.0,0.0);} void mainImage(out vec4 fragColor,in vec2 fragCoord){vec3 c;makeColor(fragCoord/iResolution.xy,c);fragColor=vec4(c,1.0);}
