# tests/integration/tmux — drive the wasm-built tmux under yos.
#
# Assumes `nix build .#tmux` (or `.#all`) has been run; discovery is
# read-only and the test SKIPs (rc 0) if the artifact isn't realized,
# so a fresh checkout still builds clean.
#
# Override the wasm path with YOS_TMUX_WASM=/abs/path when iterating
# without nix.

py3 = import('python').find_installation('python3')

tmux_int_env = environment()
tmux_int_env.set('YOS_REPO_ROOT', meson.project_source_root())
if host_machine.system() == 'windows'
  tmux_build_basename = run_command(
    py, '-c', 'import sys, os; print(os.path.basename(sys.argv[1]))',
    meson.project_build_root(), check : true).stdout().strip()
else
  tmux_build_basename = run_command('basename', meson.project_build_root(),
                                    check : true).stdout().strip()
endif
tmux_int_env.set('YOS_BUILD_DIR', tmux_build_basename)

test('tmux-new-session',
     py3,
     args : files('test_tmux.py'),
     suite : 'integration-tmux',
     env : tmux_int_env,
     depends : [yos_exe],
     timeout : 40)
