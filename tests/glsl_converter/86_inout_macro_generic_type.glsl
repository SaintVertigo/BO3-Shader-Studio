// Generic macro parameters used as types must not be treated as concrete types
// by the FXC out/inout definite-assignment pass.
#define P86(d,e) d e(inout d a,d b){d c=floor((a/b)+.5);a=(fract((a/b)+.5)-.5)*b;return c;}
P86(vec2,p86)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 p = fragCoord * .01;
    vec2 cell = p86(p, vec2(2.0));
    fragColor = vec4(p + cell, 0.0, 1.0);
}
