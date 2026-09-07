
//----------------------------------------------------------
// KoleidoskopicDistance.glsl
// Show 2d koleidoskopic projection distance.    
// mouse.x -> changes leave count
// Based on:   
//  Flower-DF:         https://www.shadertoy.com/view/MldXRN
//  Ellipse-Distance:  https://www.shadertoy.com/view/4sS3zz
//----------------------------------------------------------

const float TWO_PI  = 6.28318530718;

//----------------------------------------------------------
float sdKoleidoskopic(vec2 pos, int N) // calculate distance
{
    float st = sin(iTime*0.4);
	float ka = atan(pos.x, pos.y) / TWO_PI * float(N);
    return length(pos) - mix(0.6, 0.5+st,abs(fract(ka)-.5));
}
//----------------------------------------------------------
vec3 distanceColors (in float d)         // d=distance 
{
  vec3 color = vec3(0.1, 0.4, 0.7);      // inner color
  color = vec3(1.0) - sign(d)*color;     // + outer color
  color *= 1.0 - exp(-2.0*abs(d));       // distance darken
  color *= 0.8 + 0.2*cos(120.0*abs(d));  // distance lines 
  color = mix(color, vec3(1.0), 1.-smoothstep(0.0,0.02,abs(d)));  //white frame
  return color;  
}
//----------------------------------------------------------
void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
	vec2 uv = (fragCoord*2.0 -iResolution.xy) / iResolution.y;
    
    int leaves = 10 - int (10.*iMouse.x / iResolution);
    
    float d = sdKoleidoskopic (uv, leaves);    

    fragColor = vec4(distanceColors(d),1);
}