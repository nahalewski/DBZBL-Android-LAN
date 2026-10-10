#!/bin/sh
set -eu
mac_project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "${1-}" in
    ""|-*) mac_game_root="$mac_project_root/game_data_root" ;;
    *) mac_game_root=$1; shift ;;
esac
mac_binary="$mac_project_root/out/build/mac-arm64-release/burstlimit.app/Contents/MacOS/burstlimit"
if [ ! -x "$mac_binary" ]; then
    echo "Build first with macos/build.sh" >&2
    exit 1
fi
mkdir -p "$mac_project_root/out/mac-user-data"
mac_gamertag_file="$mac_project_root/out/mac-user-data/gamertag.txt"
if [ -f "$mac_gamertag_file" ]; then
    mac_gamertag=$(cat "$mac_gamertag_file")
    set -- "--online_gamertag=$mac_gamertag" "$@"
fi
cd "$mac_project_root"
exec "$mac_binary" "--game_data_root=$mac_game_root" \
    "--user_data_root=$mac_project_root/out/mac-user-data" \
    --gpu_plugin=xenos --frame_rate=30 --fullscreen=false \
    --window_width=1280 --window_height=720 \
    --draw_resolution_scale_x=1 --draw_resolution_scale_y=1 \
    --async_shader_compilation=false "$@"
