// Regression: converter-side macro expansion must substitute all formal
// parameters simultaneously. Sequential replacement corrupts the first
// argument here because it contains the identifier `b`, which is also the
// second macro parameter.
struct MacroPair
{
    float a;
    float b;
};

#define ADD_FIELDS(a,b) ((a) + (b))
#define MAKE_SUM_FN(T) float macroPairSum(T a,T b){return ADD_FIELDS(a.b,b.a);}
MAKE_SUM_FN(MacroPair)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    MacroPair p = MacroPair(0.25, 0.75);
    float v = macroPairSum(p, p);
    fragColor = vec4(v, v, v, 1.0);
}
