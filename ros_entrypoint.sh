#!/bin/bash
set -e

# Setup ROS 2 environment
source "/opt/ros/$ROS_DISTRO/setup.bash"

# Additionally, source the workspace if it has been built
if [ -f "/ros2_ws/install/setup.bash" ]; then
    source "/ros2_ws/install/setup.bash"
fi

# Bring up the headless D-Bus Secret Service the SmartSpectra SDK needs, and
# export the bus address into this shell so the exec-ed node inherits it.
source /usr/local/bin/unlock_keyring.sh

# Execute the passed command
exec "$@"
