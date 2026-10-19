/* batch.frag
Copyright (c) 2017 by Michael Zahniser

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
uniform float frameCount;

// Frame index to a 3D-texture coordinate at the centre of that slice (no blending between slices).
vec3 L(vec2 c, float layer) { return vec3(c, (layer + 0.5) / frameCount); }

varying vec3 fragTexCoord;
varying float fragAlpha;
void main() {
	float first = floor(fragTexCoord.z);
	float second = mod(ceil(fragTexCoord.z), frameCount);
	float fade = fragTexCoord.z - first;
	gl_FragColor = mix(
		texture3D(tex, L(fragTexCoord.xy, first)),
		texture3D(tex, L(fragTexCoord.xy, second)), fade);
	gl_FragColor *= vec4(fragAlpha);
}
