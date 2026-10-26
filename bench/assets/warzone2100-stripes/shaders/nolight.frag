// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

//#pragma debug(on)

// constants overridden by WZ when loading shaders (do not modify here in the shader source!)
#define WZ_MIP_LOAD_BIAS 0.f
//

uniform sampler2D Texture;
uniform vec4 colour;
uniform bool alphaTest;
uniform float graphicsCycle; // a periodically cycling value for special effects

uniform int fogEnabled; // whether fog is enabled
uniform float fogEnd;
uniform float fogStart;
uniform vec4 fogColor;

#if (!defined(GL_ES) && (__VERSION__ >= 130)) || (defined(GL_ES) && (__VERSION__ >= 300))
#define NEWGL
#else
#define texture(tex,uv,bias) wzStripe(tex,uv,bias)
#endif

#ifdef NEWGL
in float vertexDistance;
in vec2 texCoord;
#else
varying float vertexDistance;
varying vec2 texCoord;
#endif

#ifdef NEWGL
out vec4 FragColor;
#else
// Uses gl_FragColor
#endif

// PS3 debug (Warzone patch hunt): vertical stripes 120 px wide, repeating in 4 modes:
// 0 texture2D without bias (TEX), 1 bias -20 (TXB, level 0), 2 the game's bias (TXB), 3 no texture (grey 0.6)
vec4 wzStripe(sampler2D t, vec2 uv, float b)
{
	float m = mod(floor(gl_FragCoord.x / 120.0), 4.0);
	if (m < 0.5) return texture2D(t, uv);
	if (m < 1.5) return texture2D(t, uv, -20.0);
	if (m < 2.5) return texture2D(t, uv, b);
	return vec4(0.6, 0.6, 0.6, 1.0);
}

void main()
{
	vec4 texColour = texture(Texture, texCoord, WZ_MIP_LOAD_BIAS);

	vec4 fragColour = texColour * colour;

	if (alphaTest && (fragColour.a <= 0.001))
	{
		discard;
	}
	
	if (fogEnabled > 0)
	{
		// Calculate linear fog
		float fogFactor = (fogEnd - vertexDistance) / (fogEnd - fogStart);

		if(fogFactor > 1.f)
		{
			discard;
		}

		// Return fragment color
		fragColour = mix(fragColour, vec4(fogColor.xyz, fragColour.w), clamp(fogFactor, 0.0, 1.0));
	}

	#ifdef NEWGL
	FragColor = fragColour;
	#else
	gl_FragColor = fragColour;
	#endif
}
