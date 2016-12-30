#![no_std]
#![feature(alloc)]
#![feature(collections)]
#![feature(lang_items)]

#[lang = "eh_personality"] extern fn eh_personality() {}
#[lang = "eh_unwind_resume"] extern fn eh_unwind_resume() {}



extern crate freertos_rs;

use freertos_rs::*;
use ctypes::c_void;
use serial::Serial;

mod hal;
mod ctypes;
mod serial;


#[no_mangle]
pub extern fn main_entry() {
    let mut s = Serial::new();

    s.tx_string("Hello from Rust!\n");

    loop {
        let data = s.rx_i32();

        s.tx_string("Received: ");
        s.tx_i32(data);
        s.tx_string("\n");
    }

}






extern {
    fn pvPortMalloc(size: u32) -> *mut c_void;
    fn pvPortRealloc(p: *mut c_void, size: u32) -> *mut c_void;
    fn vPortFree(p: *mut c_void);
}

#[no_mangle]
pub extern fn __rust_allocate(size: usize, align: usize) -> *mut u8 {
	unsafe { pvPortMalloc(size as u32) as *mut u8 }
}

#[no_mangle]
pub extern fn __rust_deallocate(ptr: *mut u8, old_size: usize, align: usize) {
	unsafe { vPortFree(ptr as *mut c_void) }
}

#[no_mangle]
pub extern fn __rust_reallocate(ptr: *mut u8, old_size: usize, size: usize, align: usize) -> *mut u8 {
	unsafe { pvPortRealloc(ptr as *mut c_void, size as u32) as *mut u8 }
}

#[no_mangle]
pub extern fn __exidx_start() {
    loop {}
}

#[no_mangle]
pub extern fn __exidx_end() {
    loop {}
}

#[no_mangle]
pub extern fn _kill() {
    loop {}
}

#[no_mangle]
pub extern fn _exit() {
    loop {}
}

#[no_mangle]
pub extern fn _getpid() {
    loop {}
}
