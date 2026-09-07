uniform sampler2D iChannel0; void mainImage(out vec4 fragColor,in vec2 fragCoord){vec2 uv=fragCoord/iResolution.xy;fragColor=textureLod(iChannel0,uv,1.0);}
