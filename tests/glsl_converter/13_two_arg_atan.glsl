void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 p=fragCoord-iResolution.xy*0.5;float a=atan(p.y,p.x);fragColor=vec4(vec3(a/6.28318+0.5),1.0);}
