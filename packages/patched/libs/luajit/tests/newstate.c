#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
int main(void)
{
	lua_State *L = luaL_newstate();
	if (!L) { puts("luaL_newstate failed"); return 1; }
	luaL_openlibs(L);
	if (luaL_dostring(L, "local s=0 for i=1,100 do s=s+i end assert(s==5050)")) { puts("script failed"); return 2; }
	lua_close(L);
	return 0;
}
