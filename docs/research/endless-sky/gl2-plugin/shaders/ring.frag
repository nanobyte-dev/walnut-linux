/* ring.frag
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



uniform vec4 color;
uniform float radius;
uniform float width;
uniform float angle;
uniform float startAngle;
uniform float dash;
const float pi = 3.1415926535897932384626433832795;

varying vec2 coord;

void main() {
	float arc = mod(atan(coord.x, coord.y) + pi + startAngle, 2.0 * pi);
	float arcFalloff = 1.0 - min(2.0 * pi - arc, arc - angle) * radius;
	if(dash != 0.0)
	{
		arc = mod(arc, dash);
		arcFalloff = min(arcFalloff, min(arc, dash - arc) * radius);
	}
	float len = length(coord);
	float lenFalloff = width - abs(len - radius);
	float alpha = clamp(min(arcFalloff, lenFalloff), 0.0, 1.0);
	gl_FragColor = color * alpha;
}
