# Tags project

#override compile_flags += `xml2-config --cflags --libs` `mysql_config --include --libs`

src_files             := src/main src/tagfile src/sha1 src/property src/file src/item src/common src/fields src/utils src/errors src/where
test_src_files        := tests/test src/property src/item src/fields src/utils src/sha1 src/file src/where

proj_cfiles           := $(addsuffix .c,$(src_files))
proj_dfiles           := $(wildcard $(addsuffix /*.d,src))
proj_ofiles           := $(patsubst %.c,%.o,$(proj_cfiles));

test_cfiles           := $(addsuffix .c,$(test_src_files))
test_dfiles           := $(wildcard $(addsuffix /*.d,src))
test_ofiles           := $(patsubst %.c,%.o,$(test_cfiles));

.PHONY: all clean install uninstall

all: tags test

tags: $(proj_ofiles)
	gcc -g $(compile_flags) $^ -o $@

test: $(test_ofiles)
	gcc -g $(compile_flags) $^ -o $@

%.o: %.c
	gcc -Wall -Wextra -g -c -MMD $(compile_flags) $< -o $@

include $(proj_dfiles)

clean:
	rm -f $(proj_ofiles)
	rm -f $(proj_dfiles)
	rm -f $(test_ofiles)
	rm -f $(test_dfiles)

install:
	cp tags /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/tags

