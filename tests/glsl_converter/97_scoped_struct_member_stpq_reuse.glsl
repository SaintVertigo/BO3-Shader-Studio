// BO3_EXPECT_CONVERTED_CONTAINS: item.p=origin
// BO3_EXPECT_CONVERTED_CONTAINS: item.t+float2
// BO3_EXPECT_CONVERTED_CONTAINS: item.xy
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: item.z=origin
// BO3_EXPECT_CONVERTED_NOT_CONTAINS: item.y+float2

struct Payload97
{
    vec3 p;
    vec2 t;
};

vec2 vectorAlias97(vec2 item)
{
    return item.st;
}

vec3 structFields97(Payload97 item, vec3 origin)
{
    item.p=origin;
    vec2 shifted=item.t+vec2(1.0,2.0);
    return item.p+vec3(shifted,0.0);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    Payload97 item=Payload97(vec3(fragCoord,1.0),fragCoord);
    vec2 alias=vectorAlias97(fragCoord);
    fragColor=vec4(structFields97(item,vec3(alias,1.0)),1.0);
}
