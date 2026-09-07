#define D(S) (1.0 + (S 2.0))
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    float a = D( );
    float b = D(-);
    fragColor = vec4(a, b, 0.0, 1.0);
}
