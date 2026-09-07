struct Sample { float s; };

void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    vec2 z = fragCoord;
    Sample keepStructMember = Sample(0.25);
    float v = z.s + z.t + keepStructMember.s;
    fragColor = vec4(v, z.st, 1.0);
}
