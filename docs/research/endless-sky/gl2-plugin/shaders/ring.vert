/* ring.vert
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



uniform vec2 scale;
uniform vec2 position;
uniform float radius;
uniform float width;

attribute vec2 vert;
varying vec2 coord;

void main() {
	coord = (radius + width) * vert;
	gl_Position = vec4((coord + position) * scale, 0.0, 1.0);
}
