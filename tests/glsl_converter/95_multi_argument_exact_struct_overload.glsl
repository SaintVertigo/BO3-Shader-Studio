// BO3_EXPECT_NOTE_CONTAINS: multi-argument exact custom-struct overload call(s)
#define vec1 float

struct A95 { vec1 a; vec1 b; };
struct B95 { vec1 a; vec1 b; };

A95 choose95(A95 a, A95 b, A95 weight)
{
    return A95(mix(a.a, b.a, weight.a), mix(a.b, b.b, weight.b));
}

B95 choose95(B95 a, B95 b, B95 weight)
{
    return B95(mix(a.a, b.a, weight.a), mix(a.b, b.b, weight.b));
}

A95 subtract95(A95 a, A95 b) { return A95(a.a - b.a, a.b - b.b); }
B95 subtract95(B95 a, B95 b) { return B95(a.a - b.a, a.b - b.b); }

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A95 left = A95(fragCoord.x, fragCoord.y);
    A95 right = A95(fragCoord.y, fragCoord.x);
    A95 weight = A95(0.25, 0.75);
    A95 value = subtract95(choose95(left, right, weight), right);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
