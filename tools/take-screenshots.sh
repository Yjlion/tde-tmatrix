#!/bin/bash
# Takes the screenshots in screenshots/ and doc/en/ under Xvfb with the TDE
# window manager and panel, against a local Synapse filled by
# core/examples/seed.rs.
#
# Run inside the tools/Dockerfile image after building and installing
# tde-tmatrix (tools/build.sh), with the source in /src:
#   docker run --rm -v "$PWD":/src tde-tmatrix-build \
#     bash -c '/src/tools/build.sh && /src/tools/take-screenshots.sh'
set -e
export DISPLAY=:1 PATH=/opt/trinity/bin:/opt/cargo/bin:$PATH HOME=${HOME:-/root}
export XDG_CONFIG_DIRS=/opt/trinity/etc/xdg:/etc/xdg XDG_DATA_DIRS=/opt/trinity/share:/usr/share
export LANG=C.UTF-8
SRC=${SRC:-/src}
OUT=${OUT:-$SRC/screenshots}
STATE=${STATE:-/tmp/seed}
BUILD=${BUILD:-/tmp/build}
HS=http://127.0.0.1:8008
mkdir -p "$OUT" "$STATE/avatars"
CFG=$HOME/.trinity/share/config
mkdir -p "$CFG"

# ---- desktop session -------------------------------------------------------
if ! xdpyinfo >/dev/null 2>&1; then
	Xvfb $DISPLAY -screen 0 1280x860x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
	for i in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
fi
printf '[Wallet]\nEnabled=false\nFirst Use=false\n' > "$CFG/tdewalletrc"
printf '[General]\nwidgetStyle=plastik\n[WM]\nactiveBackground=48,110,165\n' > "$CFG/kdeglobals"
[ -n "$DBUS_SESSION_BUS_ADDRESS" ] || eval "$(dbus-launch --sh-syntax)"
pgrep -x tdeinit >/dev/null || tdeinit >/tmp/tdeinit.log 2>&1
pgrep -x twin >/dev/null || (twin >/tmp/twin.log 2>&1 &)
pgrep -x kicker >/dev/null || (kicker >/tmp/kicker.log 2>&1 &)
xsetroot -solid "#3a6ea5"
sleep 3

# windows of TMatrix only (the panel shows the same titles)
appwin() { local p; p=$(pgrep -x tde-tmatrix | head -1); [ -n "$p" ] && xdotool search --onlyvisible --pid "$p" --name "$1" 2>/dev/null | tail -1; }
shot() { # name window-pattern
	local id
	for i in $(seq 1 40); do id=$(appwin "$2"); [ -n "$id" ] && break; sleep 0.5; done
	if [ -z "$id" ]; then echo "MISSING window for $1"; return; fi
	sleep 1.5
	timeout 20 import -frame -window "$id" "$OUT/$1.png" && echo "saved $1"
}
key() { xdotool key --clearmodifiers "$@"; sleep 1.2; }
typetext() { xdotool type --delay 20 "$1"; sleep 0.5; }
win() { appwin "$1"; }
place() { # pattern x y w h
	local id; for i in $(seq 1 40); do id=$(win "$1"); [ -n "$id" ] && break; sleep 0.5; done
	xdotool windowmove "$id" "$2" "$3"; xdotool windowsize "$id" "$4" "$5"; sleep 1
}

# ---- homeserver and pictures -------------------------------------------------
# a fresh server, so the room directory shows only the seeded rooms
pkill -f "homeserver.yaml --daemonize" || true
sleep 2
rm -rf /tmp/synapse-shots "$STATE"
mkdir -p "$STATE/avatars"
$SRC/tools/synapse/start-synapse.sh /tmp/synapse-shots
avatar() { convert -size 256x256 "gradient:$2-$3" -gravity center -font DejaVu-Sans-Bold -pointsize 130 -fill white \
	-annotate +0+6 "$4" "$STATE/avatars/$1"; }
avatar bob.png "#f5a623" "#d0021b" B
avatar carol.png "#7ed321" "#2a8f4b" C
avatar alice.png "#bd10e0" "#4a148c" A
avatar room-dev.png "#4a90e2" "#1d3f72" "R14"
avatar room-users.png "#50e3c2" "#1d6fa8" TDE
convert -size 640x400 plasma:"#6fb1e0"-"#1e4f80" -blur 0x6 \
	\( -size 640x400 xc:none -fill "#ffffffcc" -draw "roundrectangle 120,150 520,250 20,20" \) -composite \
	-gravity center -font DejaVu-Sans-Bold -pointsize 34 -fill "#1d3f72" -annotate +0-8 "Trinity Meetup 2026" \
	-pointsize 18 -annotate +0+28 "Berlin · 24 participants" "$STATE/avatars/meetup.jpg"

SEED="$BUILD/core/target/release/examples/seed"
if [ ! -x "$SEED" ]; then
	(cd $SRC/core && CARGO_TARGET_DIR=$BUILD/core/target cargo build --release --locked --example seed)
fi
[ -f "$STATE/bob.session" ] || "$SEED" $HS "$STATE" setup

# ---- TMatrix ------------------------------------------------------------------
pkill -x tde-tmatrix || true
rm -rf $HOME/.trinity/share/apps/tde-tmatrix $CFG/tde-tmatrixrc
printf '[General]\nTrayIcon=true\n[Banners]\n' > $CFG/tde-tmatrixrc
(tde-tmatrix >/tmp/tmatrix.log 2>&1 &)
sleep 4
place "Log In to Matrix" 300 160 560 420
key shift+Tab
key ctrl+a
typetext "$HS"
key Tab
typetext "alice"
key Tab
typetext "secret-password"
sleep 2
shot login "Log In to Matrix"
key Return
sleep 8

# conversations, sent while this session is logged in so it gets the keys
"$SEED" $HS "$STATE" messages
sleep 4
DM=$(python3 -c "import json;print(json.load(open('$STATE/rooms.json'))['dm'])")
DEV=$(python3 -c "import json;print(json.load(open('$STATE/rooms.json'))['dev'])")

place " - TMatrix$" 20 10 1100 760
tde-tmatrix --room "$DM"
sleep 5
shot main-window " - TMatrix$"

# room directory
key ctrl+j
place "Explore Rooms" 150 120 820 520
sleep 2
shot directory "Explore Rooms"
key Escape

# room settings: members of the developers room
tde-tmatrix --room "$DEV"
sleep 3
shot room " - TMatrix$"
key ctrl+i
place "Room Settings" 160 100 760 520
W=$(win "Room Settings")
eval $(xdotool getwindowgeometry --shell "$W")
xdotool mousemove $((X + 45)) $((Y + 120)) click 1; sleep 1.5
shot room-settings "Room Settings"
key Escape

# verification of this session with "Alice's phone"
rm -f "$STATE/confirm"
("$SEED" $HS "$STATE" verify > /tmp/seed-verify.log 2>&1 &)
sleep 3
key alt+a
key v
for i in $(seq 60); do grep -q comparing /tmp/seed-verify.log && break; sleep 1; done
place "Verify Session" 260 200 620 300
sleep 2
shot verification "Verify Session"
touch "$STATE/confirm"
W=$(win "Verify Session")
xdotool windowactivate "$W"; sleep 0.5
key alt+t
for i in $(seq 60); do grep -q verified /tmp/seed-verify.log && break; sleep 1; done
sleep 12
key Escape

# account: encryption and sessions
key alt+a
key e
place "Manage Account" 120 100 900 520
sleep 3
shot encryption "Manage Account"
W=$(win "Manage Account")
eval $(xdotool getwindowgeometry --shell "$W")
xdotool mousemove $((X + 45)) $((Y + 120)) click 1; sleep 2
shot sessions "Manage Account"
key Escape

# settings
key alt+s
key Up
key Return
place "Configure TMatrix" 200 120 680 440
shot settings "Configure TMatrix"
key Escape

# tray icon with unread messages: the right end of the panel
tde-tmatrix --room "$DEV"
sleep 2
appwin " - TMatrix$" | xargs -r xdotool windowminimize
sleep 3
# hover the icon for its tooltip (the tray is left of the clock)
eval $(xdotool search --onlyvisible --class kicker | while read w; do xdotool getwindowgeometry --shell $w; done | tail -5 | grep -E "^(X|Y|WIDTH|HEIGHT)=")
xdotool mousemove 1143 845; sleep 3
import -window root -crop 440x190+840+670 +repage -resize 150% "$OUT/tray.png" && echo "saved tray"

# the handbook uses the same pictures
for f in main-window login directory room-settings verification encryption sessions settings; do
	[ -f "$OUT/$f.png" ] && cp "$OUT/$f.png" "$SRC/doc/en/$f.png"
done
echo done
