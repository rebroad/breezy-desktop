#!/bin/bash

# might be needed on a fresh docker setup:
#   install qemu and qemu-user-static packages
#   sudo docker context rm default

sudo docker run --privileged --rm tonistiigi/binfmt --install all
sudo docker run --rm --privileged multiarch/qemu-user-static --reset -p yes

if [[ "$1" == "--init" || ! $(sudo docker buildx inspect breezykwinbuilder &>/dev/null; echo $?) -eq 0 ]]; then
    # start fresh
    echo "Creating new docker builder instance"
    sudo docker buildx rm breezykwinbuilder 2>/dev/null || true
    sudo docker buildx create --use --name breezykwinbuilder --driver docker-container --driver-opt image=moby/buildkit:latest
else
    echo "Using existing docker builder instance"
    sudo docker buildx use breezykwinbuilder
fi

echo "Building docker image"
# sudo docker buildx build --platform linux/amd64 -f ./docker-build/Dockerfile -t "breezy-kwin:amd64" --load .
# sudo docker buildx build --platform linux/arm64 -f ./docker-build/Dockerfile -t "breezy-kwin:arm64" --load .
sudo docker buildx build --platform linux/amd64 -f ./docker-build/Dockerfile.steamos -t "breezy-kwin-steamos:amd64" --load .