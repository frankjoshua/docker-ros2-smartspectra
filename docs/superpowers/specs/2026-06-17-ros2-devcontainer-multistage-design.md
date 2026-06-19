# ROS 2 Dev Container + Deploy: Single Multi-Stage Dockerfile

**Date:** 2026-06-17
**Status:** Approved (design) — pending implementation plan
**Repo:** `docker-ros2-template`

## Motivation

The repository is a template for **quick ROS 2 development and deployment** (not optimized for
image size or build speed). It does two jobs:

1. **Develop** — a VS Code dev container (`.devcontainer/devcontainer.json`).
2. **Deploy** — a multi-arch image built by `build.sh` + GitHub Actions and pushed to a registry.

A rewrite of `devcontainer.json` (copied from the official guide
[Setup ROS 2 with VSCode and Docker — lyrical](https://docs.ros.org/en/lyrical/How-To-Guides/Setup-ROS-2-with-VSCode-and-Docker-Container.html))
pointed `build.dockerfile` at `.devcontainer/Dockerfile`, which does not exist, so the container
build failed (`ENOENT … /.devcontainer/Dockerfile`). That surfaced a deeper question: how should the
dev container and the deploy image relate?

### Core requirement: dev/deploy parity

The dev container and the deploy image **must share one source of truth for dependencies**. If a
package is installed for development but not mirrored into the deploy image (or vice versa), code can
work in the container and fail when deployed — defeating the entire reason for containerizing the
"develop locally, deploy to embedded" workflow. Two independent Dockerfiles were rejected for exactly
this reason.

## Goals

- One **multi-stage `Dockerfile`** at the repo root, `FROM frankjoshua/ros2:lyrical`, with stages
  `base` → `dev` / `prod`. Shared dependencies live in `base`, so `dev` and `prod` cannot diverge.
- Dev container follows the official lyrical guide: official-guide structure, **non-root `ros` user**,
  workspace bind-mounted and built **inside** the container (nothing baked into the dev stage).
- Deploy image keeps today's behavior: workspace baked in, `colcon build` at image-build time,
  `ros_entrypoint.sh` entrypoint, multi-arch publish via `build.sh`/CI preserved.
- Flatten the colcon workspace to **`src/` at the repo root** (the dev container binds the repo root
  to `/home/ws` and expects `src/` there).
- Fix stale CI/README references (`ros2-master`, `master` branch, old action versions).

## Non-Goals

- No image-size / build-speed optimization (explicitly out of scope per "not optimized").
- No change to the example package's behavior; it stays as the template's sample.
- No new ROS functionality.
- No arm32/v7 target (`build.sh` defaults to `linux/arm64,linux/amd64`).
- Base image `frankjoshua/ros2:lyrical` is kept as-is; we do not switch to official `ros:lyrical`.

## Target Repository Layout

```
docker-ros2-template/
├── .devcontainer/
│   └── devcontainer.json     # repointed to ../Dockerfile, target "dev", user "ros"
├── Dockerfile                # NEW shape: multi-stage base/dev/prod
├── ros_entrypoint.sh         # workspace source path updated to /ros2_ws
├── build.sh                  # adds --target prod
├── src/                      # MOVED from ros2_ws/src — repo root is the colcon workspace
│   └── example_pkg/
├── .gitignore                # NEW — ignores build/ install/ log/
├── .github/workflows/ci.yml  # branch, image name, action versions fixed
└── README.md                 # rewritten to document both workflows
```

## Component Design

### 1. `Dockerfile` (repo root, multi-stage)

```dockerfile
FROM frankjoshua/ros2:lyrical AS base
# Single source of truth for shared dependencies. Both dev and deploy inherit this.
RUN apt-get update && apt-get install -y \
        python3-pip \
        # add further shared apt packages here \
    && rm -rf /var/lib/apt/lists/*

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
    && echo '[ -f /home/ws/install/setup.bash ] && source /home/ws/install/setup.bash' >> /home/$USERNAME/.bashrc
ENV SHELL=/bin/bash
USER $USERNAME
CMD ["/bin/bash"]

# ---- prod: base + workspace baked and built ----
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
CMD ["ros2", "run", "example_pkg", "example_node"]
```

**Dependency discipline (the rule that keeps parity):** any dependency *not* expressed in a
`src/*/package.xml` (i.e. a manual `apt`/`pip` install) MUST be added to the `base` stage — never
installed ad hoc inside a running dev container, because that change would not reach `prod`.
Dependencies that *are* in a `package.xml` are installed by `rosdep` in both places (prod at build
time; dev via `postCreateCommand`), so they share the package manifests as their source of truth.

Assumption: `frankjoshua/ros2:lyrical` sets `ROS_DISTRO` and provides `/opt/ros/$ROS_DISTRO/setup.sh`
(it derives from the official ROS image). Verify during implementation.

### 2. `.devcontainer/devcontainer.json`

Keep the current guide-derived file; change only the build block and remote user:

```jsonc
{
    "name": "ROS 2 Development Container",
    "privileged": true,
    "remoteUser": "ros",
    "build": {
        "dockerfile": "../Dockerfile",
        "context": "..",
        "target": "dev",
        "args": { "USERNAME": "ros" }
    },
    "workspaceFolder": "/home/ws",
    "workspaceMount": "source=${localWorkspaceFolder},target=/home/ws,type=bind",
    "customizations": { "vscode": { "extensions": [ /* unchanged */ ] } },
    "containerEnv": {
        "DISPLAY": "unix:0",
        "ROS_AUTOMATIC_DISCOVERY_RANGE": "SUBNET",
        "ROS_DOMAIN_ID": "0"
    },
    "runArgs": ["--net=host", "--pid=host", "--ipc=host", "-e", "DISPLAY=${env:DISPLAY}"],
    "mounts": [
        "source=/tmp/.X11-unix,target=/tmp/.X11-unix,type=bind,consistency=cached",
        "source=/dev/dri,target=/dev/dri,type=bind,consistency=cached"
    ],
    "postCreateCommand": "rosdep update && rosdep install --from-paths src --ignore-src -y"
}
```

- The Dockerfile lives at the repo root (the deploy build needs that context), so the dev container
  references `../Dockerfile` with `context: ".."` and selects `target: "dev"`.
- `remoteUser` and the `USERNAME` build arg are `ros` — a generic name, not a hardcoded personal
  username (this is a template). VS Code's `updateRemoteUserUID` (default on, Linux) remaps the `ros`
  UID to the host user, so bind-mounted files are not left root-owned.
- `postCreateCommand` runs `rosdep` against `src/` (no `sudo`: cache lands in the user's `~/.ros`,
  and `rosdep install` self-sudos `apt`). No `chown` needed — VS Code's `updateRemoteUserUID` maps
  `ubuntu` to the host UID, so the bind-mounted workspace is already owned correctly.

### 3. `ros_entrypoint.sh`

Only the workspace-overlay path changes (deploy `WORKDIR` is now `/ros2_ws`, not `/root/ros2_ws`):

```bash
#!/bin/bash
set -e
source "/opt/ros/$ROS_DISTRO/setup.bash"
if [ -f "/ros2_ws/install/setup.bash" ]; then
    source "/ros2_ws/install/setup.bash"
fi
exec "$@"
```

### 4. `build.sh`

One change — select the deploy stage in the buildx invocation:

```bash
eval "docker buildx build $PUSH -t $TAG $ARCHITECTURE --target prod . $QUIET"
```

Everything else (multi-arch default, push/local flags, QEMU setup) is unchanged.

### 5. `.github/workflows/ci.yml`

- Trigger branch `master` → `main` (git default branch is `main`, so CI does not fire today). This is
  the **git branch**, distinct from the repo-name rename below.
- Publish image name: `frankjoshua/ros2-master` → `frankjoshua/ros2-template` (follows the repo rename
  `docker-ros2-master` → `docker-ros2-template`). The base image `frankjoshua/ros2:lyrical` is a
  different image and is unchanged.
- Bump stale actions: `actions/checkout@v2 → v4`, `docker/setup-qemu-action@v1 → v3`,
  `docker/setup-buildx-action@v1 → v3`, `docker/login-action@v1 → v3`.
- Reorder so checkout precedes login (minor cleanliness; optional).
- `build.sh` already passes `--target prod`, so CI needs no extra build flags.

### 6. `.gitignore` (new)

```
build/
install/
log/
```

### 7. Workspace layout migration

- `git mv ros2_ws/src/example_pkg src/example_pkg` (repo root becomes the colcon workspace).
- `git rm -r ros2_ws/build ros2_ws/install ros2_ws/log` — these baked artifacts are currently tracked
  and must leave version control; `.gitignore` keeps them out going forward.
- Remove the now-empty `ros2_ws/` directory.
- Example package keeps name `example_pkg` and executable `example_node`
  (`ros2 run example_pkg example_node`), matching the prod `CMD`.

### 8. `README.md`

Rewrite to reflect this repo. The current README is copied from the old `ros2-master` repo — it even
describes *"Runs a ros master in a Docker container,"* which no longer matches (the repo builds and
runs the example workspace).

- **Rename all stale refs** `master` → `template`: repo `docker-ros2-master` → `docker-ros2-template`,
  published image `frankjoshua/ros2-master` → `frankjoshua/ros2-template`, and the docker-pulls / CI
  badge URLs. Replace the "ros master" description with what the template actually does.
- **Develop:** open the folder in VS Code → "Dev Containers: Reopen in Container" → `colcon build`
  inside the container at `/home/ws`.
- **Deploy/publish:** `./build.sh -t frankjoshua/ros2-template -l` (local) or `-p` (push);
  CI publishes on push to `main`.
- Explain the multi-stage design (`base`/`dev`/`prod`) and the single-source-of-truth dependency rule.
- Update the "GitHub template" instructions.

## Verification

1. `devcontainer.json` is valid JSON.
2. `docker build --target dev -f Dockerfile .` succeeds; `docker build --target prod .` succeeds.
3. Dev: "Reopen in Container" builds and opens; `ros2 --help` works; `colcon build` in `/home/ws`
   succeeds; files created in the bind-mounted `src/` are owned by the host user (not root).
4. Deploy: `./build.sh -t test/ros2 -l` builds the prod image; `docker run --rm test/ros2` starts and
   runs `example_node`.
5. `git status` shows `build/ install/ log/` ignored; no build artifacts tracked.

## Decisions & Rationale

| Decision | Choice | Why |
|---|---|---|
| Base image | `frankjoshua/ros2:lyrical` (kept) | User standardizes on it everywhere. |
| One vs two Dockerfiles | **One**, multi-stage | Two files let dev/deploy deps diverge — breaks parity. |
| Multi-stage vs push deps into base image | Multi-stage in this repo | Keeps project-specific deps with the project, not in the global base. |
| Dev user | base image's default non-root `ubuntu` (UID 1000) | Simpler than creating a user (no userdel/useradd), and `ubuntu` already has the dialout/video/plugdev groups handy for robotics hardware. UID auto-remapped by VS Code so bind-mounted files aren't root-owned. Only an identity/entrypoint difference from prod — no dependency impact. (Switched from a created `ros` user post-review.) |
| Workspace layout | flatten to `src/` at root | Required by the guide's dev-container layout; simplifies the deploy `COPY`. |
