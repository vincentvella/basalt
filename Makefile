# One way in.
#
# Everything here already existed as a script, a cmake line or a python file,
# and the way to find out which was to read the README or a CI job and retype
# it. The arguments were the problem more than the names were: most of these
# want the path to a React Native checkout, and it was passed positionally to
# each one, so the same path was typed into bootstrap, build_ts, bundle and
# metro separately and could disagree between them.
#
# So this is a front door rather than a build system. The scripts still do the
# work and are still worth reading; what this adds is one place that knows the
# arguments, and a list you can print.
#
# Overridable, on the command line or from the environment:
#
#   make build RN_DIR=../react-native-0.87 BUILD=build-087 JOBS=4
#
.DEFAULT_GOAL := help

# A React Native checkout, which bootstrap.sh and the bundler both need. The
# sibling directory is where scripts/bootstrap.sh puts one and what js/
# metro.config.js already falls back to, so the default agrees with the
# existing default rather than inventing a second one.
RN_DIR ?= ../react-native

# The build tree. Named rather than hardcoded because more than one is normal
# here: a tree per React Native version is how the supported ones get tested.
BUILD ?= build

# Parallelism. nproc on Linux, sysctl on macOS, and 1 if neither answers, which
# is better than an empty -j taking the machine down.
JOBS ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# Which desktop to bundle and test for. The host built from this tree is the
# one for the machine you are on; this is the JavaScript side of the same
# choice.
PLATFORM ?= $(shell uname -s | sed 's/Darwin/macos/;s/Linux/linux/')

# One of N shards of the end-to-end suite, as `I/N`. Empty runs the whole
# suite, which is what a person wants; CI sets it per job.
SHARD ?=
ifneq ($(SHARD),)
SHARD_ARG := --shard $(SHARD)
endif

# clang on both, because docs/DECISIONS.md says GCC is not supported and
# Hermes needs clang-cl rather than cl on Windows.
CC_NAME ?= clang
CXX_NAME ?= clang++

# ccache when it is installed and nothing when it is not. Passing a launcher
# that does not exist fails every compile with "ccache: command not found",
# which is a confusing way to find out it is missing.
CCACHE := $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
LAUNCHERS := -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endif

.PHONY: help
help: ## Print this list
	@echo "basalt. Targets:"
	@echo
	@grep -hE '^[a-z][a-z0-9_-]*:.*?## ' $(MAKEFILE_LIST) \
	  | sort -t: -k1,1 \
	  | awk 'BEGIN {FS = ":.*?## "}; {printf "  %-18s %s\n", $$1, $$2}'
	@echo
	@echo "Variables:  RN_DIR=$(RN_DIR)  BUILD=$(BUILD)  JOBS=$(JOBS)  PLATFORM=$(PLATFORM)"
	@if [ -z "$(CCACHE)" ]; then \
	  echo "            ccache is not installed; builds will be cold."; \
	fi

# -- Getting a tree that can build -------------------------------------------

.PHONY: bootstrap
bootstrap: ## Fetch and prepare React Native and Hermes (slow, once)
	scripts/bootstrap.sh $(RN_DIR)

# RN_DIR is the checkout root, which is what bootstrap.sh takes. cmake wants
# the package directory inside it, so the suffix is added here rather than
# asked of whoever sets the variable.
.PHONY: configure
configure: ## Run cmake into the build tree
	cmake -B $(BUILD) -G Ninja \
	  -DCMAKE_C_COMPILER=$(CC_NAME) -DCMAKE_CXX_COMPILER=$(CXX_NAME) \
	  $(LAUNCHERS) \
	  -DRN_DIR=$(abspath $(RN_DIR))/packages/react-native

# Configure if it has not been done. The stamp is cmake's own cache, so this
# reconfigures when the cache is removed and not on every build.
$(BUILD)/CMakeCache.txt:
	@$(MAKE) configure

.PHONY: build
build: $(BUILD)/CMakeCache.txt ## Build every host this machine can
	cmake --build $(BUILD) -j $(JOBS)

.PHONY: ts
ts: ## Build the TypeScript packages, then type-check them and e2e/
	scripts/build_ts.sh $(RN_DIR)

# -- Running something -------------------------------------------------------

.PHONY: metro
metro: ## Serve the demo app to a running host
	scripts/metro.sh $(RN_DIR)

.PHONY: bundle
bundle: ## Write a production bundle for this desktop
	scripts/bundle.sh $(RN_DIR) --prod --platform $(PLATFORM) --build-dir $(BUILD)

# -- Tests -------------------------------------------------------------------
#
# Each of these is told which tree to look in. The scripts default to `build`
# on their own, so without it `make test BUILD=build-x` would build one tree
# and test another without saying so, which is the class of mistake this file
# exists to remove rather than to reproduce one level up.

.PHONY: test
test: ## Every suite this machine can run, in CI's order
	BASALT_BUILD_DIR=$(BUILD) scripts/test_all.sh

.PHONY: test-quick
test-quick: ## Unit suites only, skipping end-to-end and parity
	BASALT_BUILD_DIR=$(BUILD) scripts/test_all.sh --quick

.PHONY: test-list
test-list: ## Print the step names test_all.sh knows
	BASALT_BUILD_DIR=$(BUILD) scripts/test_all.sh --list

.PHONY: e2e
e2e: ## The end-to-end suite, against whichever host is built
	python3 scripts/integration_test.py --platform $(PLATFORM) --build-dir $(BUILD) $(SHARD_ARG)

.PHONY: compare
compare: ## Run every demo app through every host that is built
	BASALT_BUILD_DIR=$(BUILD) scripts/compare_all.sh

# APP is optional: with none, compare_hosts.sh runs its own views-only app.
.PHONY: compare-hosts
compare-hosts: ## Run one app through every host and diff the trees
	BASALT_BUILD_DIR=$(BUILD) scripts/compare_hosts.sh $(APP)

# -- The website -------------------------------------------------------------

.PHONY: docs
docs: ## Build the documentation site
	cd website && npm run build

.PHONY: docs-serve
docs-serve: ## Serve the documentation site with reload
	cd website && npm start

# No `doctor` target. `basalt doctor` reports what an *application* needs in
# order to build for the desktop, and this repository is not one: it has no
# package.json at its root and nothing here would be checked. It belongs in the
# app you are configuring, as `npx basalt doctor`.

# -- Housekeeping ------------------------------------------------------------

.PHONY: clean
clean: ## Remove the build tree
	rm -rf $(BUILD)

# Deliberately not a `clean` that also removes third_party: Hermes takes tens
# of minutes to rebuild and nothing about an ordinary clean should cost that.
.PHONY: distclean
distclean: ## Remove the build tree and the vendored Hermes build
	rm -rf $(BUILD) third_party/hermes-build
