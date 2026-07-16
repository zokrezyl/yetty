# Single source of truth for the Dawn release yetty links against, on every
# platform. Desktop/tvOS/Windows (dawn-exotic) and macOS/iOS (google/dawn) use
# both values; the Emscripten build derives its emdawnwebgpu port from
# DAWN_VERSION alone (see build-tools/yetty/libs/webgpu.cmake).
#
# google/dawn and dawn-exotic both publish v${DAWN_VERSION} from the same
# upstream commit (${DAWN_COMMIT}).
include_guard(GLOBAL)

set(DAWN_VERSION "20260714.215939" CACHE STRING "Dawn version to use")
set(DAWN_COMMIT "a7f696b69f89bdf55bfe3af7be10f7a61e4cc329" CACHE STRING "Dawn commit hash")
