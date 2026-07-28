# ydummy demos — the two halves of the codegen client/server split pilot
# (see src/yetty/ydummy/README.md).
#
#   client/  pure wire client — no GPU include path, links only the
#            generated call stubs + yclass runtime. Builds everywhere.
#   server/  standalone server — WebGPU + implementation + skels; spawns
#            the client over a socketpair, serves it, renders, verifies.

add_subdirectory(client)
add_subdirectory(server)
