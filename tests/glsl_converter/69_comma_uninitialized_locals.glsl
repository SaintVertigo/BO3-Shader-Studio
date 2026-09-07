void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec3 d = vec3(fragCoord, 1.0), p, o;
    p.z -= 6.0;
    o = p;
    fragColor = vec4(d + p + o, 1.0);
}
