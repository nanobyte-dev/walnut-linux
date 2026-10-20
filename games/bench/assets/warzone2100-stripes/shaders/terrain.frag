// Version directive is set by Warzone when loading the shader
// (This shader supports GLSL 1.20 - 1.50 core.)

// constants overridden by WZ when loading shaders (do not modify here in the shader source!)
#define WZ_MIP_LOAD_BIAS 0.f
//

uniform sampler2D tex;
uniform sampler2D lightmap_tex;

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
in vec4 color;
in vec2 uv1;
in vec2 uv2;
in float vertexDistance;
#else
varying vec4 color;
varying vec2 uv1;
varying vec2 uv2;
varying float vertexDistance;
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
	vec4 fragColor = color * texture(tex, uv1, WZ_MIP_LOAD_BIAS) * texture(lightmap_tex, uv2, 0.f);
	
	if (fogEnabled > 0)
	{
		// Calculate linear fog
		float fogFactor = (fogEnd - vertexDistance) / (fogEnd - fogStart);

		if(fogFactor > 1.f)
		{
			discard;
		}

		// Return fragment color
		fragColor = mix(fragColor, vec4(fogColor.xyz, fragColor.w), clamp(fogFactor, 0.0, 1.0));
	}

	#ifdef NEWGL
	FragColor = fragColor;
	#else
	gl_FragColor = fragColor;
	#endif
}
