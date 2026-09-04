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

# A static musl binary: no loader, no shared libraries, nothing to install.
# stderr goes to the log because a Kindle has no console, and a crash with no
# trace is indistinguishable from a device that simply stopped.
"$DIR/pocketllm" "$DIR" >> /mnt/us/pocketllm.log 2>&1
