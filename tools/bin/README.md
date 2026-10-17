Put `freerouting.jar` here (Freerouting v2.1.0, from https://github.com/freerouting/freerouting/releases)
if you want to re-autoroute the control board (`tools/build_all.sh --reroute`). The normal build re-uses
the saved route in `control_lite/build/control_lite.ses` and doesn't need it. The jar isn't in the repo
because it is 64 MB.
