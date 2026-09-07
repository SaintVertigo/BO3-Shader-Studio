// BO3_EXPECT_NOTE_CONTAINS: parenthesized member-chain type(s)
#define vec1 float

struct A93 { vec1 a; vec1 b; };
struct B93 { vec1 a; vec1 b; };

A93 cmd93(A93 a, vec1 b) { return A93(a.a, a.b*b); }
B93 cmd93(A93 a, vec2 b) { return B93(a.a, a.b*b); }

A93 su93(A93 a, vec1 b) { return A93(a.a-b, a.b); }
B93 su93(B93 a, vec1 b) { return B93(a.a-b, a.b); }

A93 apply93(A93 p, vec1 amount)
{
    return su93(cmd93(A93(abs((p).a), sign((p).a)), (p).b), amount);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A93 value = apply93(A93(fragCoord.x, fragCoord.y), 0.5);
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
