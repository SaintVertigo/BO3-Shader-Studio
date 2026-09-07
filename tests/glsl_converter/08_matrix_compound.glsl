void mainImage(out vec4 fragColor,in vec2 fragCoord){mat2 m=mat2(0.8,-0.6,0.6,0.8); vec2 uv=fragCoord/iResolution.xy; uv*=m; fragColor=vec4(uv,0.0,1.0);}
