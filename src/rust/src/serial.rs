
use hal::{serial_init, serial_baud, serial_format, serial_putc, serial_getc};
use hal::{serial_t, SERIAL_TX, SERIAL_RX, SerialParity};

pub struct Serial {
    pipe: serial_t
}

impl Serial {
    pub fn new() -> Serial {
        let mut s = Serial {
            pipe: serial_t::default()
        };
        unsafe {
            serial_init(&mut s.pipe, SERIAL_TX, SERIAL_RX);
            serial_baud(&mut s.pipe, 38400);
            serial_format(&mut s.pipe, 8, SerialParity::ParityNone, 1);
        }
        s
    }

    pub fn set_baud(&mut self, baudrate: i32) {
        unsafe {
            serial_baud(&mut self.pipe, baudrate);
        }
    }

    pub fn set_format(&mut self, bits: i32, parity: SerialParity, stop_bits: i32) {
        unsafe {
            serial_format(&mut self.pipe, bits, parity, stop_bits);
        }
    }

    pub fn tx_string(&mut self, s: &str) {
        for c in s.chars() {
            unsafe {
                serial_putc(&mut self.pipe, c as i32);
            }
        }
    }

    pub fn tx_i32(&mut self, i: i32) {
        unsafe {
            serial_putc(&mut self.pipe, i);
        }
    }

    pub fn rx_i32(&mut self) -> i32 {
        unsafe {
            serial_getc(&mut self.pipe)
        }
    }
}
