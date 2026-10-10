# tropic01-cra-openwrt -- build harness
#
# This repository is an OpenWrt package feed plus a small patch set, not a
# full OpenWrt tree. `make` clones the upstream revision pinned in
# openwrt.pin into build/openwrt, wires this repo in as a feed and builds
# the OpenWrt One image. Run `make help` for the targets.

TOP := $(CURDIR)
OPENWRT_DIR ?= $(TOP)/build/openwrt
JOBS ?= $(shell nproc)
IMAGE_DIR := $(OPENWRT_DIR)/bin/targets/mediatek/filogic
DOCKER_IMAGE ?= tropic01-cra-openwrt-builder

export OPENWRT_DIR DL_DIR

.DEFAULT_GOAL := build
.PHONY: help prepare build menuconfig savedefconfig package lint \
	docker-image docker-build test clean distclean

help:
	@echo "Targets:"
	@echo "  build          prepare, then build the OpenWrt One image (default)"
	@echo "  prepare        clone pinned OpenWrt, apply patches, set up feeds and .config"
	@echo "  package PKG=x  compile one package from this repo's feed (V=s output)"
	@echo "  menuconfig     run OpenWrt menuconfig on the prepared tree"
	@echo "  savedefconfig  print the diff of the current .config against defaults"
	@echo "  lint           shellcheck every shell script in the repo"
	@echo "  test           run the sandbox tests (router-side scripts with fake hardware, no board needed)"
	@echo "  docker-build   run 'make build' inside the builder container"
	@echo "  clean          remove build output, keep the checkout and downloads"
	@echo "  distclean      remove build/ entirely"
	@echo ""
	@echo "Variables: OPENWRT_DIR=$(OPENWRT_DIR) JOBS=$(JOBS) DL_DIR=<shared download cache>"

prepare:
	./scripts/prepare.sh

build: prepare
	$(MAKE) -C $(OPENWRT_DIR) -j$(JOBS)
	@echo ""
	@echo "Images:"
	@ls -1 $(IMAGE_DIR)/*openwrt_one* 2>/dev/null || echo "  (none found in $(IMAGE_DIR))"

package: prepare
	@test -n "$(PKG)" || { echo "usage: make package PKG=<package-name>"; exit 1; }
	$(MAKE) -C $(OPENWRT_DIR) package/feeds/tropic01/$(PKG)/compile V=s

menuconfig: prepare
	$(MAKE) -C $(OPENWRT_DIR) menuconfig

savedefconfig:
	@cd $(OPENWRT_DIR) && ./scripts/diffconfig.sh

test:
	@tests/sandbox/run-all.sh
	@features/core/tropicctl/tests/run.sh

lint:
	@files=$$(find . -path ./build -prune -o -type f \
			\( -name '*.sh' -o -path '*/files/*' -o -path '*/host-tools/*' \) -print \
		| while read -r f; do [ "$$(head -n 1 "$$f")" = '#!/bin/sh' ] && echo "$$f"; done); \
	if [ -z "$$files" ]; then \
		echo "lint: no shell scripts found"; \
	else \
		echo "$$files" | xargs shellcheck -s sh; \
	fi; \
	shellcheck -s bash -x -P SCRIPTDIR tests/sandbox/*.sh features/core/tropicctl/tests/run.sh features/core/tropicctl/tests/lib.sh

docker-image:
	docker build -t $(DOCKER_IMAGE) \
		--build-arg USER_UID=$$(id -u) --build-arg USER_GID=$$(id -g) .devcontainer

docker-build: docker-image
	docker run --rm -it --user $$(id -u):$$(id -g) -v $(TOP):/work -w /work \
		$(DOCKER_IMAGE) make build JOBS=$(JOBS) OPENWRT_DIR=/work/build/openwrt

clean:
	-$(MAKE) -C $(OPENWRT_DIR) clean

distclean:
	rm -rf $(TOP)/build
