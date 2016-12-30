
TARGET=application
OBJ_DIR=$(TARGET)/Debug/rust_obj


.PHONY: application
application:
	cd src/rust && rustup override set nightly
	cd src/rust && xargo build --target thumbv7m-none-eabi
	mkdir -p $(OBJ_DIR)
	cp src/rust/target/thumbv7m-none-eabi/debug/librustl8710.rlib $(OBJ_DIR)/librustl8710.o
