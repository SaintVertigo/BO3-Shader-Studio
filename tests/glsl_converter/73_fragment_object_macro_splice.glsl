#define hfrag vec2 h){h=fract(h)
#define gfrag greaterThan(h,vec2
#define tfrag);return float(b.x==b.y);}

float checkerFrag(hfrag;bvec2 b=gfrag(.5)tfrag

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float v = checkerFrag(fragCoord.xy * 0.01);
    fragColor = vec4(v, v, v, 1.0);
}
