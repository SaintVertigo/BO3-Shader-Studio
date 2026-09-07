// BO3_EXPECT_NOTE_CONTAINS: (pass 8)
#define vec1 float

struct A92 { vec1 a; vec1 b; };
struct B92 { vec1 a; vec1 b; };

A92 first92(A92 a, A92 b) { return a; }
B92 first92(B92 a, B92 b) { return a; }

A92 deep92(A92 value)
{
    return first92(first92(first92(first92(first92(first92(first92(first92(
        value, A92(1.0,1.0)), A92(2.0,2.0)), A92(3.0,3.0)), A92(4.0,4.0)),
        A92(5.0,5.0)), A92(6.0,6.0)), A92(7.0,7.0)), A92(8.0,8.0));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A92 value = deep92(A92(fragCoord.x, fragCoord.y));
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
