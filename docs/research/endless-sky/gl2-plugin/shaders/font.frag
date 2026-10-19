/* font.frag
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/



// The user must supply a texture and a color (white by default).
uniform sampler2D tex;
uniform vec4 color;

// This comes from the vertex shader.
varying vec2 texCoord;

// Output color.

// Multiply the texture by the user-specified color (including alpha).
void main() {
	gl_FragColor = texture2D(tex, texCoord).a * color;
}
