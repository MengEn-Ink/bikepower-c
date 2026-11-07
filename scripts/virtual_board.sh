#!/bin/sh
# macOS Apple Silicon/Linux 本地入口，不访问真实串口或蓝牙适配器。
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ACTION=${1:-doctor}
if [ "$#" -gt 0 ]; then shift; fi
TOOLS="$ROOT/build/virtual-board-tools"
PY="$TOOLS/venv/bin/python"
case "$ACTION" in
  setup)
    python3 -m venv "$TOOLS/venv"
    "$PY" -m pip install -r "$ROOT/requirements-virtual-board.txt"
    "$PY" "$ROOT/scripts/tools/virtual_board.py" install --destination "$TOOLS"
    ;;
  doctor)
    "$PY" "$ROOT/scripts/tools/virtual_board.py" doctor --emulator "$TOOLS/esp-emu"
    ;;
  build)
    : "${IDF_PATH:?请先激活ESP-IDF 5.5.4}"
    VERSION=$(idf.py --version)
    printf '%s' "$VERSION" | python3 -c 'import sys; assert "v5.5.4" in sys.stdin.read(), "需要ESP-IDF5.5.4"'
    BUILD=${1:-build/virtual-board-build}
    case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
    mkdir -p "$BUILD"
    python3 -c 'import pathlib,sys; root=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2]); out.write_text((root/"sdkconfig.defaults").read_text(encoding="utf-8")+"\nCONFIG_BOARD_GENERIC_C3=y\n",encoding="utf-8")' "$ROOT" "$BUILD/sdkconfig.defaults"
    cd "$ROOT"
    idf.py --ccache -B "$BUILD" -D "SDKCONFIG=$BUILD/sdkconfig" -D "SDKCONFIG_DEFAULTS=$BUILD/sdkconfig.defaults" -D PORTAL_TEST_AUTO_CONFIG=OFF reconfigure build
    ;;
  test|debug)
    BUILD=${1:-build/virtual-board-build}
    if [ "$#" -gt 0 ]; then shift; fi
    case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
    mkdir -p "$ROOT/build/virtual-board-validation"
    IMAGE="$ROOT/build/virtual-board-validation/firmware-$(python3 -c 'import uuid; print(uuid.uuid4().hex)').bin"
    "$PY" "$ROOT/scripts/tools/virtual_board.py" merge --build-dir "$BUILD" --output "$IMAGE"
    if [ "$ACTION" = debug ]; then
      "$PY" "$ROOT/tests/virtual_board_gdb.py" --emulator "$TOOLS/esp-emu" --firmware "$IMAGE" --elf "$BUILD/bikepower-c.elf" "$@"
    else
      GIT=$(git -C "$ROOT" rev-parse --short HEAD)
      "$PY" "$ROOT/tests/virtual_board_e2e.py" --emulator "$TOOLS/esp-emu" --firmware "$IMAGE" --elf "$BUILD/bikepower-c.elf" --expected-git "$GIT" "$@"
    fi
    ;;
  *)
    printf '%s\n' '用法：scripts/virtual_board.sh setup|doctor|build|test|debug [build-dir] [--security --running | --gdb PATH]' >&2
    exit 2
    ;;
esac
