# tests/ut/libc — guest-side libc surface tests

Each `test_*.c` is a wasm32 program that exercises one slice of the
libc-by-import contract:

- The **only** way the guest reaches anything outside its sandbox is
  through `__attribute__((import_module("env"), import_name("<name>")))`
  declarations. There is no `__yos_syscall(N, ...)` thunk, no
  number-indexed dispatch, no syscall layer. Each libc function is a
  direct wasm import resolved by yos.
- Each test compiles with `clang -target wasm32-unknown-unknown
  -nostdlib`, links with `-Wl,--no-entry -Wl,--export=_start`, and is
  run by the host `yos` binary built from this tree.
- A test "passes" if `yos <test>.wasm` exits with the expected exit
  code AND its stdout contains the expected substring.

Test files MUST start with a header comment block stating WHAT is
being verified and WHY. The WHY links to the design constraint the
test pins (e.g., "redeclaration with import_name attribute is the
mechanism the codegen relies on; if clang ever stops merging
attribute-decorated redeclarations, the whole sysroot strategy
breaks — this test is the canary").
