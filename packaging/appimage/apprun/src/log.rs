//! Verbose output for `--debug` (or CLOUDMUS_QT_DEBUG), the same switch the
//! app has. The flag stays in the arguments: the app reads it too.

use std::sync::atomic::{AtomicBool, Ordering};

static ENABLED: AtomicBool = AtomicBool::new(false);

pub fn enable(on: bool) {
    ENABLED.store(on, Ordering::Relaxed);
}

pub fn enabled() -> bool {
    ENABLED.load(Ordering::Relaxed)
}

macro_rules! debug {
    ($($arg:tt)*) => {
        if $crate::log::enabled() {
            eprintln!("cloudmus-apprun: debug: {}", format_args!($($arg)*));
        }
    };
}
pub(crate) use debug;
