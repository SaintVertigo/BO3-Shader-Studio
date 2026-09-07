void mainImage(out vec4 fragColor,in vec2 fragCoord){float lerp=0.25; float v=mix(0.0,1.0,lerp); fragColor=vec4(vec3(v),1.0);}
