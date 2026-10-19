/* sprite.frag
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/




uniform sampler3D tex;
uniform sampler3D swizzleMask;
uniform int useSwizzleMask;
uniform float frame;
uniform float frameCount;

// Frame index to a 3D-texture coordinate at the centre of that slice (no blending between slices).
vec3 L(vec2 c, float layer) { return vec3(c, (layer + 0.5) / frameCount); }
uniform int uniqueSwizzleMaskFrames;
uniform vec2 blur;
uniform mat4 swizzleMatrix;
uniform int useSwizzle;
uniform float alpha;
const int range = 5;

varying vec2 fragTexCoord;

void main() {
	float first = floor(frame);
	float second = mod(ceil(frame), frameCount);
	float fade = frame - first;
	vec4 color;
	if(blur.x == 0.0 && blur.y == 0.0)
	{
		if(fade != 0.0)
			color = mix(
				texture3D(tex, L(fragTexCoord, first)),
				texture3D(tex, L(fragTexCoord, second)), fade);
		else
			color = texture3D(tex, L(fragTexCoord, first));
	}
	else
	{
		color = vec4(0., 0., 0., 0.);
		const float divisor = float(range * (range + 2) + 1);
		for(int i = -range; i <= range; ++i)
		{
			float scale = float(range + 1 - abs(i)) / divisor;
			vec2 coord = fragTexCoord + (blur * float(i)) / float(range);
			if(fade != 0.0)
				color += scale * mix(
					texture3D(tex, L(coord, first)),
					texture3D(tex, L(coord, second)), fade);
			else
				color += scale * texture3D(tex, L(coord, first));
		}
	}
	if(useSwizzle > 0)
	{
		vec4 swizzleColor;
		swizzleColor = color * swizzleMatrix;
		if(useSwizzleMask > 0)
		{
			float swizzleMaskZ = 0.5;
			if(uniqueSwizzleMaskFrames > 0)
			{
				swizzleMaskZ = (first + 0.5) / frameCount;
			}
			float factor = texture3D(swizzleMask, vec3(fragTexCoord, swizzleMaskZ)).r;
			color = color * factor + swizzleColor * (1.0 - factor);
		}
		else
			color = swizzleColor;
	}
	gl_FragColor = color * alpha;
}
