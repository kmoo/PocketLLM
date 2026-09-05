#!/bin/bash
# Name: PocketLLM
# Author: PocketLLM
# DontUseFBInk
# Last-opened: 0

# Draws to the panel and reads touch, so it takes the screen over while the
# reader is still running. Nothing is stopped, no setting of yours is changed,
# and the framework repaints as soon as you leave.
#
# Tap the bar at the bottom to type. Tap the X at the top right to leave.

DIR=/mnt/us/extensions/pocketllm

timestamp=$(date +%s)
sed -i "s/^# Last-opened:.*/# Last-opened: $timestamp/" "$0" &

# MTP transfers do not preserve the executable bit, so restore it every launch.
chmod +x "$DIR/pocketllm" 2>/dev/null

LOG=/mnt/us/pocketllm.log

# A static musl binary: no loader, no shared libraries, nothing to install.
# stderr goes to the log because a Kindle has no console, and a crash with no
# trace is indistinguishable from a device that simply stopped.
"$DIR/pocketllm" "$DIR" >> "$LOG" 2>&1

# Give the screen back.
#
# The reader framework draws into /dev/fb0 and does not know anyone else did.
# When PocketLLM exits it has left the panel clean and white, but the framework
# will not repaint until something makes it -- so without this the home screen
# comes back looking half-finished, which is alarming even though nothing is
# wrong. Asking the app manager to start the home booklet makes it redraw.
#
# Guarded on purpose. lipc is stock firmware and has been there for years, but
# booklet names have changed across firmware generations, and a failure here
# must not matter: the panel is already clean, and the next tap brings the
# reader back regardless. Nothing is stopped, killed or reconfigured.
if command -v lipc-set-prop >/dev/null 2>&1; then
    lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home >> "$LOG" 2>&1
    echo "home redraw requested, lipc exit $?" >> "$LOG"
else
    echo "no lipc-set-prop here; the framework will repaint on the next tap" >> "$LOG"
fi
