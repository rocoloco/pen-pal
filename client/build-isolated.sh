#!/bin/sh
# Run only inside a disposable Debian trixie build container.
set -eu
export DEBIAN_FRONTEND=noninteractive
dpkg --add-architecture armhf
apt-get update -qq
apt-get install -y -qq --no-install-recommends gcc gcc-arm-linux-gnueabihf libc6-dev libc6-dev-armhf-cross libx11-dev libxi-dev libxft-dev libx11-dev:armhf libxi-dev:armhf libxft-dev:armhf
sh /work/build.sh
