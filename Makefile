all: ram_all

OS := $(shell uname)


.PHONY: ram_all
ram_all:
	@$(MAKE) -f rust.mk
	@$(MAKE) -f application.mk

.PHONY: mp
mp:
	@$(MAKE) -f application.mk mp

.PHONY: clean clean_all
clean:
	@$(MAKE) -f application.mk clean
	cd src/rust && cargo clean
clean_all:
	@$(MAKE) -f application.mk clean_all

.PHONY: flash debug ramdebug setup
setup:
	@$(MAKE) -f application.mk $(MAKECMDGOALS)

flash:
	@$(MAKE) -f application.mk flashburn

debug:
	@$(MAKE) -f application.mk debug

ramdebug:
	@$(MAKE) -f application.mk ramdebug
