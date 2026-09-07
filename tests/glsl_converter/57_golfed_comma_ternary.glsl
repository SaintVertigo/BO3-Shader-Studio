void mainImage(out vec4 O, in vec2 u)
{
    float l = 9.0, d = 0.0;
    vec2 P = vec2(0.0), keep = vec2(0.0);
    int i = -1, k = 0;
    for(; k < 3; ++k)
        P = u - vec2(float(k)),
        d = dot(P,P),
        d < l ? l = d, keep = P, i = k : k;

    i >= 0
        ? d = length(keep), O = vec4(d, 1.0, 0.0, 1.0)
        : O = vec4(0.0);
}
