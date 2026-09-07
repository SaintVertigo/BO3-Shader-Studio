// Regression: shader-golf code commonly aliases vec1 to float and uses macros
// that generate functions. After pre-expansion, the scalar vec1 declaration must
// override an older vector declaration that reused the same short identifier.
#define vec1 float
#define MAKE_SCALAR_SPLAT(z,x) z scalarToVec(vec1 a){return x(a);}

vec4 previousVector(vec4 a)
{
    return a;
}

MAKE_SCALAR_SPLAT(vec2,vec2)

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec1 a = fragCoord.x * 0.0 + 0.25;
    vec2 v = scalarToVec(a);
    fragColor = vec4(v, v);
}
