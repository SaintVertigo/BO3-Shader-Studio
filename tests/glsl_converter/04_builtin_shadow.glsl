void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 iResolution=iResolution.xy; vec2 uv=fragCoord/iResolution; fragColor=vec4(uv,0.0,1.0);}
