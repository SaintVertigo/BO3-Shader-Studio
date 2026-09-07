// BO3_EXPECT_NOTE_CONTAINS: preprocessor-safe scope tracking
#define MAKE_FN(T,N) T N(T a){return a;}
MAKE_FN(float, passthrough87)
const float BO3_CONST_AFTER_MACRO_87 = 2.0;
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    fragColor = vec4(passthrough87(BO3_CONST_AFTER_MACRO_87), 0.0, 0.0, 1.0);
}
