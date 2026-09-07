struct ray { vec3 org; vec3 dir; };
ray getRay(vec3 origin, vec3 direction)
{
    ray ray;
    ray.org = origin;
    ray.dir = normalize(direction);
    return ray;
}
void mainImage(out vec4 fragColor, in vec2 fragCoord)
{
    ray r = getRay(vec3(0.0), vec3(fragCoord, 1.0));
    fragColor = vec4(abs(r.dir), 1.0);
}
