uniform sampler2D iChannel0; void mainImage(out vec4 fragColor,in vec2 fragCoord){ivec2 p=ivec2(fragCoord);fragColor=texelFetch(iChannel0,p,0);}
