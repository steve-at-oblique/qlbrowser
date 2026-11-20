QDOS_CC ?= qdos-gcc
HOST_CC ?= cc
SOURCES = src/browser.c src/http.c src/platform.c
HEADERS = src/browser.h VERSION
VERSION_FLAG = -DQLBROWSER_VERSION='"$(shell cat VERSION)"'

.PHONY: all package host test
all: package

package: build/dist/qlbrowser build/dist/LICENSE

build/dist/LICENSE: LICENSE
	mkdir -p build/dist
	cp $< $@

build/qdos/qlbrowser: $(SOURCES) $(HEADERS)
	mkdir -p build/qdos
	$(QDOS_CC) $(VERSION_FLAG) -DQDOS -O -Wall -o $@ $(SOURCES)

build/dist/qlbrowser: build/qdos/qlbrowser tools/package_qdos.py
	python3 tools/package_qdos.py $< $@

host: build/host/qlbrowser

build/host/qlbrowser: $(SOURCES) $(HEADERS)
	mkdir -p build/host
	$(HOST_CC) $(VERSION_FLAG) -std=c89 -D_POSIX_C_SOURCE=200112L -O -Wall -Wextra -o $@ $(SOURCES)

build/host/click_test: tests/click.c $(SOURCES) $(HEADERS)
	mkdir -p build/host
	$(HOST_CC) $(VERSION_FLAG) -std=c89 -D_POSIX_C_SOURCE=200112L -O -Wall -Wextra -o $@ tests/click.c src/http.c src/platform.c

test: package host build/host/click_test
	build/host/click_test > /dev/null
	python3 tools/smoke.py
