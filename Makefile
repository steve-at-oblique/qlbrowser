QDOS_CC ?= qdos-gcc
HOST_CC ?= cc
SOURCES = src/browser.c src/http.c src/platform.c
HEADERS = src/browser.h

.PHONY: all package host test
all: package

package: build/dist/qlbrowse

build/qdos/qlbrowse: $(SOURCES) $(HEADERS)
	mkdir -p build/qdos
	$(QDOS_CC) -DQDOS -O -Wall -o $@ $(SOURCES)

build/dist/qlbrowse: build/qdos/qlbrowse tools/package_qdos.py
	python3 tools/package_qdos.py $< $@

host: build/host/qlbrowse

build/host/qlbrowse: $(SOURCES) $(HEADERS)
	mkdir -p build/host
	$(HOST_CC) -std=c89 -D_POSIX_C_SOURCE=200112L -O -Wall -Wextra -o $@ $(SOURCES)

test: package host
	python3 tools/smoke.py
