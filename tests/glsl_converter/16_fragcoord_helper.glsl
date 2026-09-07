float dither(){return fract(gl_FragCoord.x*0.06711056+gl_FragCoord.y*0.00583715);} void mainImage(out vec4 fragColor,in vec2 fragCoord){fragColor=vec4(vec3(dither()),1.0);}
