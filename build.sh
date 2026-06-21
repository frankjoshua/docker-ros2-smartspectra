#!/bin/bash

# Check that buildx is enabled
docker buildx version > /dev/null
if [[ $? -ne 0 ]]; then
    echo "Docker must have experimental features enabled"
    exit 1
fi

usage() {
    echo "Usage: $0 -t <DOCKER_TAG> [-p Push] [-l Local build] [-q Quiet]"
    echo "Builds the prod image. amd64 is always built (the SmartSpectra SDK is"
    echo "amd64-only); with -p, arm64 is then attempted as a best-effort bonus."
    exit 1
}
while getopts ":t:pql" o; do
    case "${o}" in
        t) TAG="${OPTARG}" ;;
        p) PUSH="--push" ;;
        q) QUIET="2> /dev/null" ;;
        l) LOCAL="true" ;;
        \?) echo "ERROR: Invalid option -$OPTARG"; usage ;;
    esac
done
shift $((OPTIND-1))

if [[ -z "${TAG}" ]]; then
    echo "Missing -t <DOCKER_TAG>"
    usage
fi

# NOTE: the prod image installs the proprietary SmartSpectra SDK from an
# IP-allowlisted apt repo — run this from a machine on an allowlisted network
# (GitHub's shared CI runners get 403). CI only validates smartspectra_msgs.

if [[ -n "${LOCAL}" ]]; then
    # Local: build amd64 and load it into the local Docker.
    eval "docker buildx build --load -t $TAG --platform linux/amd64 --target prod . $QUIET"
    exit $?
fi

# Multi-arch needs a buildx builder + qemu.
echo "Creating multi-platform builder."
BUILDER=$(docker buildx create --use)
docker run --rm --privileged multiarch/qemu-user-static --reset -p yes --credential yes

# amd64 first — the only arch the SDK is published for — then push it.
echo ">>> Building and pushing linux/amd64 ..."
eval "docker buildx build $PUSH -t $TAG --platform linux/amd64 --target prod . $QUIET"
ERROR_CODE=$?

# arm64 next, best effort: if the SDK ever ships an arm64 package the tag becomes
# a multi-arch manifest; if not, the amd64 image above is already pushed.
if [[ $ERROR_CODE -eq 0 ]]; then
    echo ">>> amd64 pushed. Trying linux/amd64,linux/arm64 (arm64 may have no SDK package) ..."
    if eval "docker buildx build $PUSH -t $TAG --platform linux/amd64,linux/arm64 --target prod . $QUIET"; then
        echo ">>> Multi-arch (amd64+arm64) image pushed."
    else
        echo ">>> arm64 build failed; $TAG remains the amd64 image already pushed."
    fi
fi

docker buildx rm "$BUILDER"
exit $ERROR_CODE
