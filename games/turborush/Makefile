PYTHON ?= python3
HOST_CC ?= cc
NWLINK ?= nwlink
TOOLCHAIN ?=
.PHONY: build module check assets host test test-arm clean
build module check:
	$(PYTHON) tools/build.py --nwlink "$(NWLINK)" $(if $(TOOLCHAIN),--toolchain "$(TOOLCHAIN)")
assets:
	$(PYTHON) tools/assets.py
host:
	@echo "Host tests are run by make test"
test:
	$(PYTHON) tools/test.py --cc "$(HOST_CC)"
test-arm: build
	$(PYTHON) tests/test_arm.py --nwlink "$(NWLINK)"
clean:
	$(PYTHON) -c "import shutil; shutil.rmtree('output', ignore_errors=True)"
