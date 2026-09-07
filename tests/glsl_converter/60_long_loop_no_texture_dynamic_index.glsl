void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(0.0);
    for(float i = 0.0; i < 100.0; i++)
    {
        fragColor[int(i) % 3] += sin(i + fragCoord.x * 0.0);
    }
}
