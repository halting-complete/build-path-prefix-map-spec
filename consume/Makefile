# To add a new interpreted-language example, add it to ALLCHECK_pecsplit
# To add a new compiled-language example, add the source to ALL_SOURCE, the
# output to ALLCHECK_pecsplit, then add any relevant rules to compile it.

TMPDIR = /run/shm/rb-prefix-map
T = testcases/

ALL_FORMATS = pecsplit
ALL_SOURCE = pecsplit.c pecsplit.rs
ALLCHECK_pecsplit = pecsplit.c.out pecsplit.rs.out pecsplit.js pecsplit.py

export AFL_USE_ASAN=1
AFL_MEMLIM = none
# 20971595M also seems to work with afl-gcc amd64 6.2.1-5

CC ?= cc
RUSTC ?= rustc

# Args:
# 1 key, e.g. "0" or "pecsplit.0"
# 2 suffix that indicates a test, e.g. "in" or "env"
find_testcases = \
	TESTCASES_$(1) = $(sort $(patsubst $(T)$(1).%.$(2),$(1).%,$(wildcard $(T)$(1).*.$(2))))
$(eval $(call find_testcases,0,in))
$(eval $(call find_testcases,1,in))
$(foreach format,$(ALL_FORMATS),$(eval $(call find_testcases,$(format).0,env)))
$(foreach format,$(ALL_FORMATS),$(eval $(call find_testcases,$(format).1,env)))

.PHONY: all
all: $(ALL_SOURCE:%=%.out)

%.c.out: %.c prefix_map.h
	$(CC) -std=c11 -Werror -g -o "$@" "$<"

%.rs.out: %.rs %.c.out
	# TODO: hack to avoid failing on Debian jessie, which is running on jenkins
	# remove when it uses stretch instead
	if [ -f /etc/debian_version -a "$$(echo "$$(cat /etc/debian_version) < 9.0" | bc)" -eq 1 ]; then \
		cp "$*.c.out" "$@"; \
	else \
		$(RUSTC) -W warnings -g -o "$@" "$<"; \
	fi

.PHONY: check testcases-rst
check: $(ALL_FORMATS:%=check-%)
testcases-html: $(ALL_FORMATS:%=testcases-%.html)

export TESTDIR = $(T)
check-%: check.sh
	$(MAKE) $(ALLCHECK_$*)
	set -e; for case in $(TESTCASES_0); do ./check.sh "$(ALLCHECK_$*)" $* 0 $$case $$case.$* $$case; done
	set -e; for case in $(TESTCASES_1); do ./check.sh "$(ALLCHECK_$*)" $* 1 $$case $$case.$* $$case; done
	set -e; for case in $(TESTCASES_$*.0); do ./check.sh "$(ALLCHECK_$*)" $* 0 $$case $$case $*.0; done
	set -e; for case in $(TESTCASES_$*.1); do ./check.sh "$(ALLCHECK_$*)" $* 1 $$case $$case $*.1; done

testcases-%.html: testcases-%.rst testcases-rst.css
	rst2html5 --stylesheet minimal.css,testcases-rst.css "$<" > "$@"

testcases-%.rst: testcases-rst.sh $(wildcard $(T)/*) Makefile
	rm -f "$@" "$@.tmp" && touch "$@.tmp"
	set -e; for case in $(TESTCASES_0); do ./testcases-rst.sh $* $$case $$case.$* $$case "decode valid"; done >> "$@.tmp"
	set -e; for case in $(TESTCASES_1); do ./testcases-rst.sh $* $$case $$case.$* $$case "decode invalid"; done >> "$@.tmp"
	set -e; for case in $(TESTCASES_$*.0); do ./testcases-rst.sh $* $$case $$case $*.0 "decode valid"; done >> "$@.tmp"
	set -e; for case in $(TESTCASES_$*.1); do ./testcases-rst.sh $* $$case $$case $*.1 "decode invalid"; done >> "$@.tmp"
	mv "$@.tmp" "$@"

make-afl-test-case = \
	cat testcases/$(3).$(1).env testcases/$(3).in > afl-in-$(1)/$(3).in;

afl-in-%: $(wildcard $(T)*.in) $(wildcard $(T)*.env)
	mkdir -p "$@"
	$(foreach case,$(TESTCASES_0),$(call make-afl-test-case,$*,0,$(case)))
	$(foreach case,$(TESTCASES_1),$(call make-afl-test-case,$*,1,$(case)))
	touch "$@"

.PHONY: fuzz-%
fuzz-%: %.out
	$(MAKE) afl-in-$(basename $*)
	@echo "$(CC)" | grep -i afl || \
	echo >&2 "warning: you didn't set CC=afl-gcc, fuzzing might not work"
	set -e; if test -d "afl-out-$*"; then \
	echo >&2 "afl-out-$* exists, reusing. run 'make reset-fuzz-$* to delete it."; \
	afl-fuzz -m $(AFL_MEMLIM) -i - -o "afl-out-$*" -- "./$<" -; else \
	mkdir -p $(TMPDIR); \
	ln -s "$$(mktemp -d -p $(TMPDIR))" "afl-out-$*"; \
	afl-fuzz -m $(AFL_MEMLIM) -i "afl-in-$(basename $*)" -o "afl-out-$*" -- "./$<" -; fi

.PHONY: reset-fuzz-%
reset-fuzz-%:
	rm -rf "$$(readlink -f "afl-out-$*")" && rm -rf "afl-out-$*"
	rmdir -p "$(TMPDIR)" 2>/dev/null || true

.PHONY: clean
clean:
	rm -f $(ALL_SOURCE:%=%.out)
	rm -rf $(ALL_FORMATS:%=afl-in-%)
	rm -f *.rst *.html

.PHONY: clean-all
clean-all: clean $(ALL_SOURCE:%=reset-fuzz-%)
