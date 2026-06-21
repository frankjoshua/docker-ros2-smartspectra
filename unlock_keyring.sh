#!/usr/bin/env bash
# unlock_keyring.sh — bring up the headless D-Bus Secret Service the SmartSpectra
# SDK requires, and export the bus address into the calling shell.
#
# Why this exists: the SmartSpectra SDK persists its device-identity secret
# through libsecret, which needs a D-Bus session bus + an unlocked gnome-keyring
# Secret Service. Containers and robots start with neither. We run our OWN bus on
# a fixed address — NOT the inherited systemd / VS Code bus, whose keyring is
# locked with a login password we never set (touching it pops a password prompt)
# — and unlock a fresh keyring with an empty passphrase.
#
# SOURCE this, don't execute it: it exports DBUS_SESSION_BUS_ADDRESS so the
# `ros2 run` that follows can reach the keyring. It is idempotent and safe to
# source from every shell — it reuses one bus instead of stacking daemons.
#   .bashrc            sources it for dev  (VS Code shells bypass the entrypoint)
#   ros_entrypoint.sh  sources it for prod/robot before exec-ing the node
#
# Every command is guarded so it can't abort a `set -e` caller (the entrypoint).

# D-Bus needs a valid machine-id to spawn a bus.
if [ ! -s /etc/machine-id ]; then
    if [ "$(id -u)" -eq 0 ]; then dbus-uuidgen --ensure=/etc/machine-id || true
    else sudo dbus-uuidgen --ensure=/etc/machine-id || true; fi
fi

# Our own private bus, at a fixed path so all shells share one instance.
export DBUS_SESSION_BUS_ADDRESS="unix:path=${XDG_RUNTIME_DIR:-/tmp}/smartspectra-bus"

# Start the bus + unlock the keyring once; later sources reuse the running one.
if ! dbus-send --reply-timeout=2000 --print-reply --dest=org.freedesktop.DBus \
        / org.freedesktop.DBus.ListNames >/dev/null 2>&1; then
    rm -f "${DBUS_SESSION_BUS_ADDRESS#unix:path=}" 2>/dev/null || true
    dbus-daemon --session --address="$DBUS_SESSION_BUS_ADDRESS" --fork --nopidfile --nosyslog 2>/dev/null || true
    echo "" | gnome-keyring-daemon --unlock --components=secrets >/dev/null 2>&1 || true
fi
