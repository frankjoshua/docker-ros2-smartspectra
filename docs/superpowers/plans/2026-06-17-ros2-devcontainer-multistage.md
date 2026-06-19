# ROS 2 Dev Container + Deploy (Single Multi-Stage Dockerfile) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify this template's VS Code dev container and its multi-arch deploy image behind one multi-stage `Dockerfile` so the development and deployment environments share a single dependency source of truth.

**Architecture:** One root `Dockerfile` with three stages — `base` (shared deps, `FROM frankjoshua/ros2:lyrical`), `dev` (base + non-root `ros` user + shell, opened by VS Code), and `prod` (base + workspace baked & `colcon build`-ed, published by `build.sh`/CI). The colcon workspace is flattened so the repo root is the workspace (`src/` at root), matching the official lyrical dev-container guide.

**Tech Stack:** Docker (multi-stage build, `docker buildx`), VS Code Dev Containers, ROS 2 (lyrical), `colcon`, `rosdep`, GitHub Actions, bash.

**Spec:** `docs/superpowers/specs/2026-06-17-ros2-devcontainer-multistage-design.md`

## Global Constraints

These apply to every task. Exact values, copied from the spec:

- **Base image:** `frankjoshua/ros2:lyrical` for every stage's lineage. Do **not** switch to official `ros:lyrical`.
- **One Dockerfile** at the repo root, multi-stage. Shared dependencies go **only** in the `base` stage — never add a dependency to just `dev` or just `prod`.
- **Dev user:** the base image's default `ubuntu` (UID 1000), with passwordless sudo added. (Switched from a created `ros` user post-review — simpler, and `ubuntu` is already in the dialout/video/plugdev groups handy for robotics hardware.)
- **Workspace layout:** repo root is the colcon workspace; packages live in `src/`. Dev container `workspaceFolder` is `/home/ws`.
- **Deploy image:** `WORKDIR /ros2_ws`; entrypoint sources `/ros2_ws/install/setup.bash`.
- **Published image name:** `frankjoshua/ros2-template`.
- **CI trigger branch:** `main`.
- **Example package:** name `example_pkg`, executable `example_node` (`ros2 run example_pkg example_node`).
- **Not optimized:** do not add image-size or build-speed optimizations.
- **Commit trailer:** every commit message ends with
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`.
- **Branch:** work happens on `feat/devcontainer-multistage-dockerfile` (already created and checked out).

> **Build prerequisite for verification:** Docker must be able to pull `frankjoshua/ros2:lyrical` (public, or `docker login` first). All image builds run on the host, not inside the dev container.

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `src/example_pkg/` | The template's example colcon package | Move from `ros2_ws/src/example_pkg/` |
| `.gitignore` | Keep colcon `build/ install/ log/` out of git | Create |
| `Dockerfile` | Multi-stage `base`/`dev`/`prod` build | Rewrite |
| `ros_entrypoint.sh` | Source ROS + workspace for the prod image | Modify (path) |
| `.devcontainer/devcontainer.json` | Open the `dev` stage as the dev container | Modify (build block, remoteUser) |
| `build.sh` | Multi-arch build/push of the `prod` stage | Modify (one line) |
| `.github/workflows/ci.yml` | CI build/publish on `main` | Modify (branch, image, action versions, order) |
| `README.md` | Document the dev + deploy workflows | Rewrite |

---

## Task 1: Flatten the colcon workspace to the repo root

**Files:**
- Move: `ros2_ws/src/example_pkg/` → `src/example_pkg/`
- Delete (tracked artifacts): `ros2_ws/build/`, `ros2_ws/install/`, `ros2_ws/log/`
- Create: `.gitignore`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: `src/` at repo root containing `example_pkg` (relied on by the Dockerfile `prod` stage `COPY src ./src` and the dev container's `--from-paths src`); a `.gitignore` ignoring `build/ install/ log/`.

- [ ] **Step 1: Verify the starting state (this is the "failing test")**

Run:
```bash
test -d ros2_ws/src/example_pkg && echo "FOUND nested ros2_ws/src" || echo "already flat"
git ls-files ros2_ws/build ros2_ws/install ros2_ws/log | head -1
```
Expected: prints `FOUND nested ros2_ws/src`, and at least one tracked artifact path (e.g. `ros2_ws/build/...`).

- [ ] **Step 2: Move the package and remove tracked build artifacts**

Run:
```bash
git mv ros2_ws/src/example_pkg src/example_pkg
git rm -r --quiet ros2_ws/build ros2_ws/install ros2_ws/log
rm -rf ros2_ws            # clear any leftover untracked colcon files + the now-empty dir
```

- [ ] **Step 3: Create `.gitignore`**

Create `.gitignore` with exactly:
```gitignore
# colcon build artifacts (repo root is the workspace)
build/
install/
log/
```

- [ ] **Step 4: Verify the new layout (the "passing test")**

Run:
```bash
test -f src/example_pkg/package.xml && echo "OK package moved"
test ! -e ros2_ws && echo "OK ros2_ws gone"
grep -qx 'build/' .gitignore && grep -qx 'install/' .gitignore && grep -qx 'log/' .gitignore && echo "OK gitignore"
```
Expected: `OK package moved`, `OK ros2_ws gone`, `OK gitignore`.

- [ ] **Step 5: Commit**

```bash
# Step 2's `git mv`/`git rm` already staged the move + deletions; this adds the new .gitignore.
# Use explicit paths (NOT `git add -A`) so unrelated working-tree changes are not swept in.
git add .gitignore src
git commit -m "refactor: flatten colcon workspace to repo root (src/) + gitignore artifacts" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Rewrite `Dockerfile` as multi-stage + fix entrypoint path

**Files:**
- Rewrite: `Dockerfile`
- Modify: `ros_entrypoint.sh` (workspace overlay path `/root/ros2_ws` → `/ros2_ws`)

**Interfaces:**
- Consumes: `src/` at repo root (Task 1).
- Produces: a `Dockerfile` with build targets `base`, `dev`, and `prod`. `dev` → non-root `ros` user + `bash`. `prod` → `WORKDIR /ros2_ws`, workspace built, `ENTRYPOINT ["/ros_entrypoint.sh"]`, `CMD ["ros2","run","example_pkg","example_node"]`. The `dev` target is consumed by Task 3; `prod` by Task 4.

- [ ] **Step 1: Confirm there is no `dev` target yet (the "failing test")**

Run:
```bash
docker build --target dev -t ros2-template:dev . 2>&1 | tail -3
```
Expected: FAIL — the current single-stage Dockerfile has no `dev` stage (error like `target stage "dev" could not be found`).

- [ ] **Step 2: Write the new multi-stage `Dockerfile`**

Replace the entire contents of `Dockerfile` with:
```dockerfile
FROM frankjoshua/ros2:lyrical AS base
# Single source of truth for shared dependencies. Both `dev` and `prod` inherit this stage,
# so they cannot drift apart. Any dependency NOT declared in a src/*/package.xml must be added
# here — never installed ad hoc inside a running dev container (that change would not reach prod).
RUN apt-get update && apt-get install -y \
        python3-pip \
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
CMD ["ros2", "run", "example_pkg", "example_node"]
```

> If a build error shows `colcon: not found` or `rosdep: command not found`, the base image lacks build tooling; add `python3-colcon-common-extensions python3-rosdep` to the `base` stage's `apt-get install` line and re-run. (Not expected — `frankjoshua/ros2:lyrical` is the existing build base.)

- [ ] **Step 3: Update `ros_entrypoint.sh` workspace path**

Replace the entire contents of `ros_entrypoint.sh` with:
```bash
#!/bin/bash
set -e

# Setup ROS 2 environment
source "/opt/ros/$ROS_DISTRO/setup.bash"

# Additionally, source the workspace if it has been built
if [ -f "/ros2_ws/install/setup.bash" ]; then
    source "/ros2_ws/install/setup.bash"
fi

# Execute the passed command
exec "$@"
```

- [ ] **Step 4: Build both stages (the "passing test")**

Run:
```bash
docker build --target dev  -t ros2-template:dev  .
docker build --target prod -t ros2-template:prod .
```
Expected: both builds succeed (`naming to docker.io/library/ros2-template:dev` / `:prod`, exit 0).

- [ ] **Step 5: Smoke-test the prod image (workspace built + entrypoint sources it)**

Run:
```bash
docker run --rm ros2-template:prod ros2 pkg executables example_pkg
```
Expected: prints `example_pkg example_node` (proves the workspace built and the entrypoint sourced `install/setup.bash`).

- [ ] **Step 6: Verify the dev image is non-root**

Run:
```bash
docker run --rm ros2-template:dev whoami
```
Expected: `ros`.

- [ ] **Step 7: Commit**

```bash
git add Dockerfile ros_entrypoint.sh
git commit -m "feat: multi-stage Dockerfile (base/dev/prod) on frankjoshua/ros2:lyrical" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Point the dev container at the `dev` stage

**Files:**
- Modify: `.devcontainer/devcontainer.json`

**Interfaces:**
- Consumes: the `dev` build target and the `ros` user from Task 2.
- Produces: a dev container that builds `../Dockerfile` with `target: dev`, `context: ..`, `remoteUser: ros`.

- [ ] **Step 1: Replace `.devcontainer/devcontainer.json`**

The file currently has interim edits. Replace its entire contents with:
```json
{
    "name": "ROS 2 Development Container",
    "privileged": true,
    "remoteUser": "ros",
    "build": {
        "dockerfile": "../Dockerfile",
        "context": "..",
        "target": "dev",
        "args": {
            "USERNAME": "ros"
        }
    },
    "workspaceFolder": "/home/ws",
    "workspaceMount": "source=${localWorkspaceFolder},target=/home/ws,type=bind",
    "customizations": {
        "vscode": {
            "extensions": [
                "ms-vscode.cpptools",
                "ms-vscode.cpptools-themes",
                "twxs.cmake",
                "donjayamanne.python-extension-pack",
                "eamodio.gitlens",
                "ms-iot.vscode-ros"
            ]
        }
    },
    "containerEnv": {
        "DISPLAY": "unix:0",
        "ROS_AUTOMATIC_DISCOVERY_RANGE": "SUBNET",
        "ROS_DOMAIN_ID": "0"
    },
    "runArgs": [
        "--net=host",
        "--pid=host",
        "--ipc=host",
        "-e", "DISPLAY=${env:DISPLAY}"
    ],
    "mounts": [
        "source=/tmp/.X11-unix,target=/tmp/.X11-unix,type=bind,consistency=cached",
        "source=/dev/dri,target=/dev/dri,type=bind,consistency=cached"
    ],
    "postCreateCommand": "rosdep update && rosdep install --from-paths src --ignore-src -y"
}
```

- [ ] **Step 2: Verify it is valid JSON and references resolve**

Run:
```bash
python3 -c "import json; c=json.load(open('.devcontainer/devcontainer.json')); print('valid'); assert c['build']['target']=='dev'; assert c['remoteUser']=='ros'"
test -f .devcontainer/../Dockerfile && echo "OK dockerfile path resolves"
```
Expected: `valid` then `OK dockerfile path resolves` (no assertion error).

- [ ] **Step 3: Commit**

```bash
git add .devcontainer/devcontainer.json
git commit -m "feat: dev container builds the dev stage as non-root ros user" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Select the `prod` stage in `build.sh`

**Files:**
- Modify: `build.sh` (the `docker buildx build` line)

**Interfaces:**
- Consumes: the `prod` build target from Task 2.
- Produces: `build.sh` that builds and (optionally) pushes the `prod` image. Consumed by Task 5 (CI calls `build.sh`).

- [ ] **Step 1: Confirm `--target` is absent (the "failing test")**

Run:
```bash
grep -n 'buildx build' build.sh
```
Expected: shows the line WITHOUT `--target prod`:
`eval "docker buildx build $PUSH -t $TAG $ARCHITECTURE . $QUIET"`

- [ ] **Step 2: Add `--target prod` to the buildx invocation**

In `build.sh`, change the line:
```bash
eval "docker buildx build $PUSH -t $TAG $ARCHITECTURE . $QUIET"
```
to:
```bash
eval "docker buildx build $PUSH -t $TAG $ARCHITECTURE --target prod . $QUIET"
```

- [ ] **Step 3: Verify by building locally through the script (the "passing test")**

Run:
```bash
./build.sh -t ros2-template:prod -l
```
Expected: local build succeeds (exit 0); `docker images ros2-template:prod` shows the image.

- [ ] **Step 4: Commit**

```bash
git add build.sh
git commit -m "feat: build.sh targets the prod stage" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Fix the CI workflow

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `build.sh` (Task 4) and the published image name `frankjoshua/ros2-template`.
- Produces: a workflow that builds/pushes `frankjoshua/ros2-template` on push to `main`.

- [ ] **Step 1: Replace `.github/workflows/ci.yml`**

Replace its entire contents with:
```yaml
---
name: CI
on:
  workflow_dispatch:
  pull_request:
  push:
    branches:
      - main
  schedule:
    # Run once a month
    - cron: '1 2 3 * *'

env:
  DOCKER_CONTAINER: frankjoshua/ros2-template

jobs:
  docker:
    name: Docker
    runs-on: ubuntu-latest
    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Set up QEMU
        uses: docker/setup-qemu-action@v3

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3

      - name: Login to DockerHub
        uses: docker/login-action@v3
        with:
          username: ${{ secrets.DOCKERHUB_USERNAME }}
          password: ${{ secrets.DOCKERHUB_TOKEN }}

      - name: Build and push image to DockerHub
        run: ./build.sh -t $DOCKER_CONTAINER -p -a linux/arm64,linux/amd64
```

- [ ] **Step 2: Verify branch + image name + no stale refs (the "passing test")**

Run:
```bash
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml')); print('valid yaml')" 2>/dev/null \
  || echo "yaml lib absent — skipping parse"
grep -q 'frankjoshua/ros2-template' .github/workflows/ci.yml && echo "OK image name"
grep -q -- '- main' .github/workflows/ci.yml && echo "OK branch main"
! grep -qi master .github/workflows/ci.yml && echo "OK no master refs"
```
Expected: `OK image name`, `OK branch main`, `OK no master refs`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: publish frankjoshua/ros2-template on main; bump action versions" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Rewrite `README.md`

**Files:**
- Rewrite: `README.md`

**Interfaces:**
- Consumes: the final layout, image name, and workflows from Tasks 1–5.
- Produces: documentation only (no downstream consumer).

- [ ] **Step 1: Confirm stale refs exist (the "failing test")**

Run:
```bash
grep -ci 'ros2-master\|ros master' README.md
```
Expected: a non-zero count.

- [ ] **Step 2: Replace `README.md`**

Replace its entire contents with:
````markdown
# ROS 2 Template [![CI](https://github.com/frankjoshua/docker-ros2-template/workflows/CI/badge.svg)](https://github.com/frankjoshua/docker-ros2-template/actions) [![](https://img.shields.io/docker/pulls/frankjoshua/ros2-template)](https://hub.docker.com/r/frankjoshua/ros2-template)

A GitHub template for quick ROS 2 **development** and **deployment**. It gives you a VS Code dev
container to work in and a multi-architecture image to ship — both built from a single multi-stage
`Dockerfile`, so what you develop against is exactly what you deploy.

## How it works

One `Dockerfile`, three stages, all `FROM frankjoshua/ros2:lyrical`:

- **`base`** — shared dependencies. Add every extra apt/pip package here so dev and deploy can't
  drift apart.
- **`dev`** — `base` + a non-root `ros` user + an interactive shell. This is what VS Code opens. Your
  workspace is bind-mounted (not copied) and you build it inside the container.
- **`prod`** — `base` + your `src/` copied in and `colcon build`-ed, with an entrypoint that runs the
  example node. This is what `build.sh` / CI publish.

```
.
├── .devcontainer/devcontainer.json   # opens the dev stage
├── Dockerfile                        # base / dev / prod
├── build.sh                          # multi-arch build + push (prod stage)
├── ros_entrypoint.sh                 # sources ROS + workspace for the prod image
└── src/                              # your colcon packages (repo root is the workspace)
    └── example_pkg/
```

## Develop

1. Install Docker, VS Code, and the **Dev Containers** extension.
2. Open this folder in VS Code.
3. `Ctrl+Shift+P` → **Dev Containers: Reopen in Container**. The first build pulls the base image.
4. In the container terminal, build and run the example:
   ```
   colcon build --symlink-install
   source install/setup.bash
   ros2 run example_pkg example_node
   ```

The repo root is the colcon workspace (`/home/ws` in the container), so `build/`, `install/`, and
`log/` appear here and are git-ignored.

## Deploy (build & publish a multi-arch image)

`build.sh` builds the `prod` stage for amd64 + arm64 with `docker buildx`.

Local single-arch build:
```
./build.sh -t frankjoshua/ros2-template -l
```

Multi-arch build and push to Docker Hub:
```
./build.sh -t frankjoshua/ros2-template -p
```

GitHub Actions publishes on every push to `main` (see `.github/workflows/ci.yml`). It expects the
`DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN` repository secrets.

Run the published image (host networking is needed because ROS 2 DDS uses ephemeral ports;
`--ipc=host` enables shared-memory transport between containers; `--pid=host` keeps DDS GUIDs unique):
```
docker run -it --network=host --ipc=host --pid=host frankjoshua/ros2-template
```

## Use as a template

This repo is a GitHub template. After creating your own repo from it:

- Add your packages under `src/`.
- Put shared dependencies in the `base` stage of the `Dockerfile`.
- Update the image name in `.github/workflows/ci.yml` (`DOCKER_CONTAINER`) and the `build.sh`
  commands above.
- Set the `DOCKERHUB_USERNAME` / `DOCKERHUB_TOKEN` repository secrets if you want CI to publish.

## License

Apache 2.0

## Author

Joshua Frank [@frankjoshua77](https://www.twitter.com/@frankjoshua77) · [roboticsascode.com](http://roboticsascode.com)
````

- [ ] **Step 3: Verify no stale refs remain (the "passing test")**

Run:
```bash
! git grep -qi 'ros2-master' -- README.md && echo "OK no ros2-master"
grep -q 'frankjoshua/ros2-template' README.md && echo "OK template name"
grep -q 'Reopen in Container' README.md && echo "OK dev docs"
```
Expected: `OK no ros2-master`, `OK template name`, `OK dev docs`.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: rewrite README for dev-container + multi-arch deploy workflows" \
           -m "Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: End-to-end verification

**Files:** none (verification gate; no commit).

**Interfaces:**
- Consumes: everything from Tasks 1–6.
- Produces: confidence that dev and deploy build from the same base and that no stale refs remain.

- [ ] **Step 1: Clean rebuild of both stages**

Run:
```bash
docker build --no-cache --target dev  -t ros2-template:dev  .
docker build --no-cache --target prod -t ros2-template:prod .
```
Expected: both succeed.

- [ ] **Step 2: Prod runs the example node end to end**

Run:
```bash
docker run --rm ros2-template:prod ros2 pkg executables example_pkg
```
Expected: `example_pkg example_node`.

- [ ] **Step 3: Repo-wide stale-ref sweep**

Run:
```bash
git grep -ni 'ros2-master\|docker-ros2-master' -- . ':!docs/superpowers' || echo "OK: no stale name refs"
git grep -n -- 'branches:' .github/workflows/ci.yml -A2 | grep -q main && echo "OK: CI on main"
```
Expected: `OK: no stale name refs` and `OK: CI on main`.

- [ ] **Step 4: Manual dev-container check (human, in VS Code)**

Not scriptable — perform in VS Code:
1. Open the folder → **Dev Containers: Reopen in Container**; confirm it builds and opens.
2. In the container terminal: `whoami` → `ros`; `colcon build --symlink-install` succeeds at `/home/ws`.
3. On the **host**, confirm the generated `build/ install/ log/` are owned by your host user (not root).

- [ ] **Step 5: Confirm the working tree is clean**

Run:
```bash
git status --short --untracked-files=no
```
Expected: empty (all tracked changes committed across Tasks 1–6; an unrelated untracked `.canvas/` may remain and is fine).

---

## Self-Review (completed by plan author)

- **Spec coverage:** Dockerfile multi-stage (T2), dev/deploy parity via shared `base` (T2 + constraints), non-root `ros` (T2/T3), `src/` flatten + `.gitignore` + `git rm` artifacts (T1), entrypoint path (T2), `build.sh --target prod` (T4), CI branch/name/versions (T5), README rewrite incl. `master`→`template` (T6), verification (every task + T7). All spec sections map to a task.
- **Placeholder scan:** no TBD/TODO; every file step shows complete content; the only conditional ("if colcon not found") gives the exact remedy.
- **Type consistency:** target names `dev`/`prod`, user `ros`, path `/ros2_ws`, image `frankjoshua/ros2-template`, package `example_pkg`/`example_node`, branch `main` used identically across all tasks and the Global Constraints.
