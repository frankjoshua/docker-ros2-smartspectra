# ROS 2 Template [![CI](https://github.com/frankjoshua/docker-ros2-template/workflows/CI/badge.svg)](https://github.com/frankjoshua/docker-ros2-template/actions) [![](https://img.shields.io/docker/pulls/frankjoshua/ros2-template)](https://hub.docker.com/r/frankjoshua/ros2-template)

A GitHub template for quick ROS 2 **development** and **deployment**. It gives you a VS Code dev
container to work in and a multi-architecture image to ship — both built from a single multi-stage
`Dockerfile`, so what you develop against is exactly what you deploy.

## How it works

One `Dockerfile`, three stages, all `FROM frankjoshua/ros2:lyrical`:

- **`base`** — shared dependencies. Add every extra apt/pip package here so dev and deploy can't
  drift apart.
- **`dev`** — `base` + the image's non-root `ubuntu` user (with passwordless sudo) + an interactive shell. This is what VS Code opens. Your
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
4. Open a terminal — ROS is already sourced, so `ros2` works immediately. Build and run the example:
   ```
   colcon build --symlink-install
   source install/setup.bash   # or just open a new terminal — the workspace overlay auto-sources
   ros2 run example_pkg example_node
   ```

The repo root is the colcon workspace (`/home/ws` in the container), so `build/`, `install/`, and
`log/` appear here and are git-ignored. The container runs as the non-root **`ubuntu`** user, which
is already in the `dialout`/`video`/`plugdev` groups — handy for serial devices and cameras.

## Multiple nodes & local-network discovery

Nodes can talk to each other — on this machine or across your LAN — out of the box. The dev
container (`.devcontainer/devcontainer.json`) sets:

- **`--net=host --ipc=host --pid=host`** (`runArgs`): host networking for LAN discovery; shared
  memory for same-host transport (**`--ipc=host` is required** — without a shared `/dev/shm`, Fast
  DDS instances silently fail to connect); and unique DDS GUIDs across containers.
- **`ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`** (`containerEnv`): discover nodes anywhere on the local
  subnet, not just this host. Use `LOCALHOST` to restrict discovery to this machine.
- **`ROS_DOMAIN_ID=0`** (`containerEnv`): only nodes sharing this ID discover each other. Give each
  project/person a unique ID to stay isolated on a shared LAN.

### Quick pub/sub test

In two terminals — same container, two containers, or two machines on the LAN:

```
# A — publisher
ros2 topic pub /chatter std_msgs/msg/String "{data: hello}"

# B — subscriber
ros2 topic echo /chatter
```

`ros2 topic list` and `ros2 node list` should show the other side. Launch another instance as its
own container with the same flags:

```
docker run -it --net=host --ipc=host --pid=host frankjoshua/ros2-template
```

> **Multicast:** `SUBNET` discovery uses multicast — reliable on wired LANs, but some Wi-Fi/cloud
> networks block it. If two machines can't discover each other there, run a Fast DDS Discovery
> Server and point nodes at it with `ROS_DISCOVERY_SERVER=<host-ip>:11811`.

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
