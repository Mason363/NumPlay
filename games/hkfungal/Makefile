# Hollow Knight: Fungal Wastes, the second part of Hollow Knight, sharing its saves. Its code and data are
# Hollow Knight's (games/hollowknight, PART=2); this builds it.
#   make            output/hkfungal.nwa
#   make check      link it like the calculator does (output/hkfungal.bin)
NWLINK ?= npx --yes -- nwlink@1.0.0
BUILD_DIR = output

.PHONY: build check clean
build check:
	$(MAKE) -C ../hollowknight $@ PART=2 NWLINK="$(NWLINK)" BUILD_DIR=$(abspath $(BUILD_DIR))

clean:
	rm -rf $(BUILD_DIR)
