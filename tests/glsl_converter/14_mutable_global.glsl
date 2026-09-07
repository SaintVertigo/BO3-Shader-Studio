float gValue=0.0; float f(float x){gValue=x;return gValue;} void mainImage(out vec4 fragColor,in vec2 fragCoord){fragColor=vec4(vec3(f(fract(iTime))),1.0);}
