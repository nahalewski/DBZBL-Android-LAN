#!/bin/sh
set -eu
mac_project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mac_interface=$(/sbin/route -n get default | /usr/bin/awk '/interface:/ {print $2; exit}')
mac_lan_ip=$(/usr/sbin/ipconfig getifaddr "$mac_interface")
if [ -z "$mac_lan_ip" ]; then
    echo "Connect the Mac to the same network as the Pixel first." >&2
    exit 1
fi
echo "Mac LAN host: $mac_lan_ip"
echo "Mac: Online Battle > Player Match > Create Match; Private Session OFF."
echo "Pixel: LAN is on by default; Online Battle > Player Match > Custom Match."
echo "Select this Mac's name; no host IP entry is required."
exec /usr/bin/env -u REX_XNET_BIND_IP -u REX_XNET_SEARCH_IP -u REX_LAN_DISABLED \
    REX_XNET_IP="$mac_lan_ip" \
    "$mac_project_root/macos/run.sh" "$mac_project_root/game_data_root" \
    --mnk_mode=true --vulkan_dynamic_rendering=false \
    --online_mode=direct --online_input_delay=off \
    --online_tick_sleep=0 --online_fast_tick=true \
    --vulkan_debug_draw_census=false --vulkan_debug_resolve_summary=false \
    --vulkan_debug_ps52d9_white=false --vulkan_debug_ps52d9_white_output=false \
    --depth_of_field=false --glow_blur=false --motion_blur=false --soft_filter=false \
    "$@"
