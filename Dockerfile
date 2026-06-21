FROM frankjoshua/ros2:jazzy AS base
# Single source of truth for shared dependencies. Both `dev` and `prod` inherit this stage,
# so they cannot drift apart. Any dependency NOT declared in a src/*/package.xml must be added
# here — never installed ad hoc inside a running dev container (that change would not reach prod).
RUN apt-get update && apt-get install -y \
        python3-pip wget\
    && rm -rf /var/lib/apt/lists/*

# SmartSpectra C++ SDK (https://smartspectra.presagetech.com/docs/cpp/linux/ubuntu-24-04/).
# Adds Presage's apt repo and installs libsmartspectra-dev (pulls the Vulkan loader itself).
# dbus + gnome-keyring: the SDK persists a device-identity secret via libsecret, which needs a
# D-Bus session + an unlocked Secret Service even in API-key mode — see unlock_keyring.sh.
# amd64 only for now — arm64 is not yet published, so this build arg can't be flipped to arm64.
RUN install -d -m 0755 /etc/apt/keyrings \
    && curl -fsSL https://packages.presagetech.com/KEY.gpg \
        | gpg --dearmor -o /etc/apt/keyrings/presage-archive-keyring.gpg \
    && chmod 644 /etc/apt/keyrings/presage-archive-keyring.gpg \
    && echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/presage-archive-keyring.gpg] https://packages.presagetech.com/apt/ubuntu noble main" \
        > /etc/apt/sources.list.d/presage-technologies.list \
    && apt-get update \
    && apt-get install -y libsmartspectra-dev dbus gnome-keyring \
    && rm -rf /var/lib/apt/lists/*

# Headless Secret Service bootstrap, baked to a fixed path so both the dev .bashrc
# and the prod ros_entrypoint.sh can source it. See README "Headless device identity".
COPY unlock_keyring.sh /usr/local/bin/unlock_keyring.sh
RUN chmod +x /usr/local/bin/unlock_keyring.sh

# ---- dev: what VS Code opens. Reuse the image's default non-root user (uid 1000, "ubuntu"),
# already in the dialout/video/plugdev groups handy for robotics hardware; just add passwordless
# sudo. VS Code remaps its UID to the host user so bind-mounted files aren't left root-owned.
FROM base AS dev
ARG USERNAME=ubuntu
RUN echo "$USERNAME ALL=(root) NOPASSWD:ALL" > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME
# VS Code terminals open an interactive shell that bypasses the image ENTRYPOINT, so source the ROS
# environment (and the workspace overlay, once built) from .bashrc — otherwise `ros2` isn't on PATH.
RUN echo 'source /opt/ros/$ROS_DISTRO/setup.bash' >> /home/$USERNAME/.bashrc \
    && echo '[ -f /home/ws/install/setup.bash ] && source /home/ws/install/setup.bash' >> /home/$USERNAME/.bashrc \
    && echo 'source /usr/local/bin/unlock_keyring.sh' >> /home/$USERNAME/.bashrc
ENV SHELL=/bin/bash
USER $USERNAME
CMD ["/bin/bash"]

# ---- prod: base + workspace baked and built. ----
FROM base AS prod
WORKDIR /ros2_ws
COPY src ./src
RUN apt-get update \
    && rosdep update \
    && rosdep install --from-paths src --ignore-src -r -y \
    && rm -rf /var/lib/apt/lists/*
RUN . /opt/ros/$ROS_DISTRO/setup.sh \
    && colcon build --symlink-install
COPY ros_entrypoint.sh /ros_entrypoint.sh
RUN chmod +x /ros_entrypoint.sh
ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["ros2", "run", "smartspectra", "smartspectra"]
