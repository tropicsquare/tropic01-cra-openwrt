# Build environment for tropic01-cra-openwrt: upstream OpenWrt build
# dependencies on Debian stable, plus shellcheck for `make lint`.
# OpenWrt refuses to build as root, so everything runs as `builder`.
FROM debian:bookworm

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	build-essential clang flex bison g++ gawk gcc-multilib g++-multilib \
	gettext git libncurses-dev libssl-dev python3 python3-setuptools \
	python3-pyelftools rsync swig unzip zlib1g-dev file wget curl \
	ca-certificates zstd xz-utils bzip2 patch perl time which \
	shellcheck openssl iproute2 \
	&& rm -rf /var/lib/apt/lists/*

ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd -g ${USER_GID} builder && \
	useradd -m -u ${USER_UID} -g ${USER_GID} -s /bin/bash builder
USER builder
WORKDIR /work
