// BO3 HLSL Previewer optional PostFX filter support.
// Compatible with the LG-RZ/BlackOps3Shaders filter constant layout.
#define SCRIPT_VECTOR_0                         0
#define SCRIPT_VECTOR_1                         4
#define SCRIPT_VECTOR_2                         8
#define SCRIPT_VECTOR_3                         12
#define SCRIPT_VECTOR_4                         16
#define SCRIPT_VECTOR_5                         20
#define SCRIPT_VECTOR_6                         24
#define SCRIPT_VECTOR_7                         28
#define SCRIPT_VECTOR_X                         0
#define SCRIPT_VECTOR_Y                         1
#define SCRIPT_VECTOR_Z                         2
#define SCRIPT_VECTOR_W                         3

// BO3 stock shared.gsh currently reserves filter slots 0-5 and 7 for game effects.
// Slot 6 is intentionally used by BO3 Shader Studio so a persistent custom PostFX
// does not collide with stock postfx bundles, which use filter index 0.
#define BO3HLSL_FILTER_INDEX_PERSISTENT          6
