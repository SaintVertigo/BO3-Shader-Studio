void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 uv=mod(fragCoord,vec2(7.0,11.0)); fragColor=vec4(uv/11.0,0.0,1.0);}
