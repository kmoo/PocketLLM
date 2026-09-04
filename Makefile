# PocketLLM — a local language model on a jailbroken Kindle.
#
#   make deps        fetch fonts, stb_truetype and llama.cpp   (once)
#   make test        host tests: layout, hit tests, transcript  (< 1s)
#   make screens     render every screen to PNGs at device size
#   make app         the ARM binary, no model  (links in seconds)
#   make app-model   the ARM binary with llama.cpp -- what you install
#   make package     the lot: ./build.sh, ready to copy to the Kindle
#   make abi         assert the ELF matches the Kindle's contract

CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O1 -g
OUT    := out

# --- the Kindle target -----------------------------------------------------
# armv7-a hard-float with NEON, statically linked against musl: no glibc symbol
# versioning, no libstdc++, no shared-library ABI surface at all. The device
# runs a 32-bit kernel, so every AArch64-gated fast path in ggml falls through
# to scalar C -- that is why it is slow, and there is no flag that fixes it.
ZIG        ?= zig
ARM_TARGET := arm-linux-musleabihf
ARM_CPU    := generic+v7a+neon
ARM_CFLAGS := -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -O2 -static

LLAMA      := vendor/llama.cpp
LLAMA_BUILD:= $(LLAMA)/build-kindle
LLAMA_LIBS := $(LLAMA_BUILD)/src/libllama.a \
              $(LLAMA_BUILD)/ggml/src/libggml.a \
              $(LLAMA_BUILD)/ggml/src/libggml-cpu.a \
              $(LLAMA_BUILD)/ggml/src/libggml-base.a

PLATFORM_HOST := platform/draw.c platform/host/ui_host.c
PLATFORM_ARM  := platform/draw.c platform/kindle/ui_fb.c \
                 platform/kindle/input_evdev.c platform/devroot.c
APP_SRC       := app/main.c app/chat.c app/models.c app/screens.c

.PHONY: all deps test screens app app-model llama package abi ask ask-arm clean
all: test

$(OUT):
	@mkdir -p $(OUT)

deps:
	@sh tools/fetch-deps.sh

# --- tests -----------------------------------------------------------------
# Both of these are pure functions given real inputs. They are fast because
# they have to be run on every change: the keyboard has already shipped once
# with two keys drawn off the right edge of the screen.
$(OUT)/t_layout: tests/test_layout.c app/screens.c app/models.c $(PLATFORM_HOST) | $(OUT)
	@$(CC) $(CFLAGS) -o $@ $^

$(OUT)/t_chat: tests/test_chat.c app/chat.c model/model_none.c | $(OUT)
	@$(CC) $(CFLAGS) -o $@ $^

$(OUT)/t_models: tests/test_models.c app/models.c | $(OUT)
	@$(CC) $(CFLAGS) -o $@ $^

test: $(OUT)/t_layout $(OUT)/t_chat $(OUT)/t_models
	@fail=0; for t in $^; do ./$$t || fail=1; done; \
	 [ $$fail -eq 0 ] && echo "PASS" || { echo "FAIL"; exit 1; }

# --- looking at it without a Kindle ----------------------------------------
# The same drawing code the device runs, against an in-memory buffer written
# out as PNG at true panel size. Only the data is stubbed.
screens: | $(OUT)
	@test -s assets/fonts/Literata.ttf || { echo "run: make deps"; exit 1; }
	@mkdir -p $(OUT)/screens
	@$(CC) -std=c11 -Wall -O1 -o $(OUT)/render tools/render-screens.c \
	   app/screens.c app/models.c $(PLATFORM_HOST)
	@./$(OUT)/render
	@echo "wrote $(OUT)/screens/*.png"

# --- the device binary -----------------------------------------------------
app: | $(OUT)
	@$(ZIG) cc -target $(ARM_TARGET) -mcpu=$(ARM_CPU) $(ARM_CFLAGS) \
	   -o $(OUT)/pocketllm $(APP_SRC) $(PLATFORM_ARM) model/model_none.c
	@sh tools/check-abi.sh $(OUT)/pocketllm | tail -2

# llama.cpp for the device.
#   GGML_NATIVE  off: we are cross-compiling, so it must not probe this Mac.
#                The CPU features are named explicitly by ARM_CPU instead.
#   GGML_LLAMAFILE off: its sgemm.cpp calls vld1q_f16, which needs the ARMv8
#                half-precision extension. This CPU is ARMv7 and does not have
#                it, so that file will not compile at all -- and the tinyBLAS
#                path it provides is AArch64-gated anyway, so nothing is lost.
llama:
	@test -d $(LLAMA) || { echo "run: make deps"; exit 1; }
	@test -f $(LLAMA_BUILD)/src/libllama.a || { \
	  echo "building llama.cpp for armv7 (this takes a few minutes)..."; \
	  cmake -B $(LLAMA_BUILD) -S $(LLAMA) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_C_COMPILER="$(CURDIR)/tools/cross/zig-cc" \
	    -DCMAKE_CXX_COMPILER="$(CURDIR)/tools/cross/zig-c++" \
	    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
	    -DBUILD_SHARED_LIBS=OFF -DLLAMA_CURL=OFF -DLLAMA_BUILD_TESTS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_SERVER=OFF \
	    -DGGML_NATIVE=OFF -DGGML_LLAMAFILE=OFF -DGGML_OPENMP=OFF -DGGML_LTO=OFF && \
	  cmake --build $(LLAMA_BUILD) --target llama -j8; }

# -Wl,-s strips: it takes the binary from ~45 MB to ~3 MB, and the debug info
# buys nothing on a device that cannot run a debugger. Diagnosis happens
# through pocketllm.log instead.
app-model: llama | $(OUT)
	@$(ZIG) c++ -target $(ARM_TARGET) -mcpu=$(ARM_CPU) -O2 -static -Wl,-s \
	   -I$(LLAMA)/include -I$(LLAMA)/ggml/include \
	   -o $(OUT)/pocketllm $(APP_SRC) $(PLATFORM_ARM) model/model_llama.c $(LLAMA_LIBS)
	@sh tools/check-abi.sh $(OUT)/pocketllm | tail -2

abi:
	@sh tools/check-abi.sh $(OUT)/pocketllm

# --- packaging -------------------------------------------------------------
# One path, so there is nothing to keep in sync: ./build.sh checks the tools,
# fetches what is missing, builds, downloads the models and lays out
# dist/COPY-TO-KINDLE ready to copy onto the device.
package:
	@sh build.sh $(MODELS)

clean:
	@rm -rf $(OUT)

# --- reading what it actually says -----------------------------------------
# Native, so a conversation takes seconds instead of the minutes it takes on
# the device. The Kindle is the arbiter of speed and memory; this is the
# arbiter of whether the replies are any good.
HOST_LLAMA := $(LLAMA)/build-host

# CPU only: Metal and BLAS would need extra frameworks in the link and are
# pointless for a 0.5B model that already answers in about a second. ggml's CPU
# backend still calls into Accelerate on a Mac, hence the conditional.
ASK_LDLIBS := $(if $(filter Darwin,$(shell uname -s)),-framework Accelerate,)
ask: | $(OUT)
	@test -d $(LLAMA) || { echo "run: make deps"; exit 1; }
	@test -f $(HOST_LLAMA)/src/libllama.a || { \
	  echo "building llama.cpp for this machine (once)..."; \
	  cmake -B $(HOST_LLAMA) -S $(LLAMA) -DCMAKE_BUILD_TYPE=Release \
	    -DLLAMA_CURL=OFF -DBUILD_SHARED_LIBS=OFF -DLLAMA_BUILD_TESTS=OFF \
	    -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_SERVER=OFF \
	    -DGGML_METAL=OFF -DGGML_BLAS=OFF -DGGML_OPENMP=OFF >/dev/null && \
	  cmake --build $(HOST_LLAMA) --target llama -j8 >/dev/null; }
	@$(CXX) -O2 -x c -I$(LLAMA)/include -I$(LLAMA)/ggml/include \
	   tools/ask.c app/chat.c model/model_llama.c -x none \
	   $(HOST_LLAMA)/src/libllama.a $(HOST_LLAMA)/ggml/src/libggml.a \
	   $(HOST_LLAMA)/ggml/src/libggml-cpu.a $(HOST_LLAMA)/ggml/src/libggml-base.a \
	   $(ASK_LDLIBS) -o $(OUT)/ask
	@echo 'built out/ask -- try: ./out/ask dist/pocketllm/model.gguf "hello"'

# The same conversation, on real 32-bit ARM, in the container. Slow and the
# timings mean nothing -- but it is the actual code the Kindle runs, which the
# host build is not.
ask-arm: llama | $(OUT)
	@$(ZIG) c++ -target $(ARM_TARGET) -mcpu=$(ARM_CPU) -O2 -static \
	   -I$(LLAMA)/include -I$(LLAMA)/ggml/include \
	   -o $(OUT)/ask-arm tools/ask.c app/chat.c model/model_llama.c $(LLAMA_LIBS)
	@sh tools/check-abi.sh $(OUT)/ask-arm | tail -1
