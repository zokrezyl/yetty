/* lua-codegen — host Lua interpreter with nvim's `nlua0` (and friends:
 * bit, mpack, lpeg) statically baked into package.preload. Used as
 * LUA_PRG by the wasm32 cross build to run neovim's build-time codegen
 * scripts that normally load nlua0 as a .so via dlopen.
 *
 * Build: linked with lua sources, lpeg sources, nvim's mpack/*.c, nlua0.c
 * and bit.c — see nvim/build.sh for the exact compile.
 *
 * Argv handling mirrors stock lua's command-line: -l <mod>, -e <chunk>,
 * then a script file followed by its args. arg[0] = script path (Lua
 * convention). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

extern int luaopen_nlua0(lua_State *L);
extern int luaopen_bit(lua_State *L);
extern int luaopen_mpack(lua_State *L);
extern int luaopen_lpeg(lua_State *L);

static void preload_one(lua_State *L, const char *name, lua_CFunction fn)
{
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

static void preload_modules(lua_State *L)
{
    lua_getglobal(L, "package");
    preload_one(L, "nlua0", luaopen_nlua0);
    preload_one(L, "bit",   luaopen_bit);
    preload_one(L, "mpack", luaopen_mpack);
    preload_one(L, "lpeg",  luaopen_lpeg);
    lua_pop(L, 1);
}

static int report_error(lua_State *L)
{
    fprintf(stderr, "lua-codegen: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
    return 1;
}

static int do_require(lua_State *L, const char *mod)
{
    lua_getglobal(L, "require");
    lua_pushstring(L, mod);
    return lua_pcall(L, 1, 1, 0) ? 1 : (lua_pop(L, 1), 0);
}

static int do_string(lua_State *L, const char *chunk)
{
    return luaL_loadstring(L, chunk) || lua_pcall(L, 0, 0, 0);
}

int main(int argc, char **argv)
{
    lua_State *L = luaL_newstate();
    if (!L) { fprintf(stderr, "lua-codegen: out of memory\n"); return 1; }
    luaL_openlibs(L);
    preload_modules(L);

    /* Walk argv, processing -l <mod> and -e <chunk> until we hit a script
     * path or run out of args. */
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') break;
        if (!strcmp(a, "--")) { i++; break; }
        if (!strcmp(a, "-l") && i + 1 < argc) {
            if (do_require(L, argv[++i])) return report_error(L);
        } else if (!strcmp(a, "-e") && i + 1 < argc) {
            if (do_string(L, argv[++i])) return report_error(L);
        } else if (!strcmp(a, "-v")) {
            fprintf(stderr, "lua-codegen (Lua 5.1)\n");
        } else {
            fprintf(stderr, "lua-codegen: unknown flag '%s'\n", a);
            return 2;
        }
    }

    if (i >= argc) { lua_close(L); return 0; }

    /* Script + remaining args. arg[0] = script, arg[1..] = rest. */
    const char *script = argv[i++];
    lua_newtable(L);
    lua_pushstring(L, script);
    lua_rawseti(L, -2, 0);
    for (int j = 0; i < argc; i++, j++) {
        lua_pushstring(L, argv[i]);
        lua_rawseti(L, -2, j + 1);
    }
    lua_setglobal(L, "arg");

    if (luaL_loadfile(L, script) || lua_pcall(L, 0, 0, 0))
        return report_error(L);

    lua_close(L);
    return 0;
}
