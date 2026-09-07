// BO3_EXPECT_CONVERTED_CONTAINS: mul(float3(uv,1.0), mul(turnX99(
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: ))*float3(uv,1.0)

mat3 turnX99(float angle)
{
    float s=sin(angle),c=cos(angle);
    return mat3(1.0,0.0,0.0,0.0,c,-s,0.0,s,c);
}

mat3 turnY99(float angle)
{
    float s=sin(angle),c=cos(angle);
    return mat3(c,0.0,-s,0.0,1.0,0.0,s,0.0,c);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 uv=fragCoord/iResolution.xy;
    vec3 direction=normalize(turnY99(iTime)*turnX99(iTime*0.5)*vec3(uv,1.0));
    fragColor=vec4(direction,1.0);
}
