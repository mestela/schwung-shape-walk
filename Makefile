SCHWUNG_ROOT ?= ../schwung
CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror
RELEASE_VERSION ?= 0.7.0

.PHONY: test device package clean

test:
	$(CC) $(CFLAGS) -I$(SCHWUNG_ROOT)/src tests/test_shape_walk.c -o tests/test_shape_walk
	./tests/test_shape_walk
	$(CC) $(CFLAGS) -I$(SCHWUNG_ROOT)/src tests/test_shape_walk_seq.c -o tests/test_shape_walk_seq
	./tests/test_shape_walk_seq
	node tests/test_overtake_ui.mjs

device:
	aarch64-linux-gnu-gcc $(CFLAGS) -fPIC -shared -I$(SCHWUNG_ROOT)/src src/dsp/shape_walk.c -o src/dsp.so
	aarch64-linux-gnu-gcc $(CFLAGS) -fPIC -shared -I$(SCHWUNG_ROOT)/src src/overtake/dsp/shape_walk_seq.c -o src/overtake/dsp.so

package:
	mkdir -p dist/shape-walk
	cp src/module.json src/dsp.so dist/shape-walk/
	tar -czf dist/shape-walk-module.tar.gz -C dist shape-walk
	mkdir -p dist/shape-walk-seq
	cp src/overtake/module.json src/overtake/ui.js src/overtake/dsp.so dist/shape-walk-seq/
	tar -czf dist/shape-walk-seq-module.tar.gz -C dist shape-walk-seq
	mkdir -p dist/shape-walk-v$(RELEASE_VERSION)/midi_fx/shape-walk
	mkdir -p dist/shape-walk-v$(RELEASE_VERSION)/tools/shape-walk-seq
	cp dist/shape-walk/module.json dist/shape-walk/dsp.so dist/shape-walk-v$(RELEASE_VERSION)/midi_fx/shape-walk/
	cp dist/shape-walk-seq/module.json dist/shape-walk-seq/ui.js dist/shape-walk-seq/dsp.so dist/shape-walk-v$(RELEASE_VERSION)/tools/shape-walk-seq/
	tar -czf dist/shape-walk-v$(RELEASE_VERSION).tar.gz -C dist/shape-walk-v$(RELEASE_VERSION) midi_fx tools

clean:
	rm -f tests/test_shape_walk tests/test_shape_walk_seq src/dsp.so src/overtake/dsp.so
