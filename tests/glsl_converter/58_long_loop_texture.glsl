#define STEPS 100
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv = fragCoord / iResolution.xy;
    vec4 c = vec4(0.0);
    for(int i = 0; i < STEPS; ++i)
    {
        c += texture(iChannel0, uv + float(i) * 0.00001);
        if(c.x > 1000.0) break;
    }
    fragColor = c / float(STEPS);
}
