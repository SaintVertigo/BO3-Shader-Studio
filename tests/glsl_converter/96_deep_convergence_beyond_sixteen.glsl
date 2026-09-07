// BO3_EXPECT_NOTE_CONTAINS: (pass 18)
#define vec1 float

struct A96 { vec1 a; vec1 b; };
struct B96 { vec1 a; vec1 b; };

A96 fold96(A96 a, A96 b) { return a; }
B96 fold96(B96 a, B96 b) { return a; }

A96 deep96(A96 value)
{
    return fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(fold96(value, A96(1.0, 1.0)), A96(2.0, 2.0)), A96(3.0, 3.0)), A96(4.0, 4.0)), A96(5.0, 5.0)), A96(6.0, 6.0)), A96(7.0, 7.0)), A96(8.0, 8.0)), A96(9.0, 9.0)), A96(10.0, 10.0)), A96(11.0, 11.0)), A96(12.0, 12.0)), A96(13.0, 13.0)), A96(14.0, 14.0)), A96(15.0, 15.0)), A96(16.0, 16.0)), A96(17.0, 17.0)), A96(18.0, 18.0));
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    A96 value = deep96(A96(fragCoord.x, fragCoord.y));
    fragColor = vec4(value.a, value.b, 0.0, 1.0);
}
