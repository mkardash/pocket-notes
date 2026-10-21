# PocketNotes
#   make test      - build and run unit tests on the Mac
#   make sim       - build the Mac simulator that renders screens to PNG files
#   make device    - cross-compile build/device/PocketNotes.app for PocketBook (needs Docker + SDK volume)
#   make install   - copy the app to a PocketBook mounted at /Volumes/PB700K3

CORE := src/core/utf8.c src/core/doc.c src/core/layout.c src/core/book.c src/core/storage.c src/core/keys.c src/core/settings.c src/core/mdstyle.c src/core/fmtview.c src/core/fmtedit.c
UI   := $(wildcard src/ui/*.c)
TESTS := tests/test_main.c tests/test_doc.c tests/test_layout.c tests/test_book.c tests/test_keys.c tests/test_md.c tests/test_fmtview.c tests/test_fmtedit.c

HOST_CFLAGS := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -g -fsanitize=undefined

PB_VOLUME ?= /Volumes/PB700K3
APP_NAME  := PocketNotes.app

.PHONY: test sim device install clean icons

test: build/tests
	./build/tests

build/tests: $(CORE) $(TESTS) src/core/*.h tests/test.h
	@mkdir -p build
	cc $(HOST_CFLAGS) $(CORE) $(TESTS) -o $@

sim: build/sim

build/sim: $(CORE) $(UI) src/host/*.c src/host/include/inkview.h src/core/*.h src/ui/*.h
	@mkdir -p build
	cc $(HOST_CFLAGS) -DHOST_SIM -Isrc/host/include $(CORE) $(UI) src/host/*.c -lm -o $@

device:
	docker run --rm --platform linux/amd64 -v pbsdk:/BUILD -v "$(CURDIR):/work" \
	  -e LD_LIBRARY_PATH=/BUILD/SDK-B300-6.8/usr/lib pocketnotes-build sh tools/build-device.sh

install: device
	cp build/device/$(APP_NAME) "$(PB_VOLUME)/applications/$(APP_NAME)"
	@echo "Installed to $(PB_VOLUME)/applications/$(APP_NAME)"

# Regenerates src/ui/icons_data.{h,c} from the Lucide font (output is committed).
icons:
	@mkdir -p build
	cc -O2 -w tools/icons/gen_icons.c -o build/gen_icons -lm
	./build/gen_icons third_party/lucide/lucide.ttf tools/icons/lucide_icons.txt src/ui/icons_data.h src/ui/icons_data.c

clean:
	rm -rf build
