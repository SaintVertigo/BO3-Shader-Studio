
mat2 rot(float a){return mat2(cos(a),-sin(a),sin(a),cos(a));}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{

    vec3 col;
    float t;
    
    for(int c=5;c<8;c++){
	    vec2 uv = (fragCoord*13.-iResolution.xy)/iResolution.y;
        t = iTime+float(c)/1.;
        for(int i=0;i<2;i++)
        {
        	uv=abs(uv);
        	uv-=1.;
        	uv=uv*rot(t/float(i+7));
        }
        
        col[c]= step(.4,fract(uv.x*13.));

	}
    
    fragColor = vec4(vec3(col),4.0);
    
}