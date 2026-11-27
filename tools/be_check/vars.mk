# tools/be_check.py: a game's port variables as its Makefile sets them for TARGET (as make has them, quotes kept)
.PHONY: be-vars
be-vars:
	$(info SRC=$(PORT_SRC))
	$(info ASM=$(PORT_ASM))
	$(info CXX=$(PORT_CXX))
	$(info FLAGS=$(PORT_FLAGS))
	$(info CXXFLAGS=$(PORT_CXXFLAGS))
	$(info DEPS=$(PORT_DEPS))
	$(info COMMON=$(PORT_COMMON))
	$(info ID=$(PORT_ID))
	$(info NAME=$(PORT_NAME))
	@:
