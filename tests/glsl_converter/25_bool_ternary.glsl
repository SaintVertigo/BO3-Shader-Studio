void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 uv=fragCoord/iResolution.xy;bool animate=uv.x>0.5;vec2 p=animate?0.5+0.5*sin(iTime+uv):uv;fragColor=vec4(p,0.0,1.0);}
