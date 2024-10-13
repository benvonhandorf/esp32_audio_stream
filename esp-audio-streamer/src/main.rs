use anyhow::Result;

use esp_idf_svc::hal::{
    self,
    gpio::{Gpio43, Gpio46},
    peripherals::Peripherals,
};
use hal::{gpio::*, i2s::config::*, i2s::I2S0, i2s::*};

use std::thread;
use std::{
    borrow::BorrowMut,
    sync::mpsc::{self, Receiver, Sender},
    sync::Arc,
    sync::Mutex,
};

fn configure_i2s<'a>(
    i2s0: I2S0,
    clk_gpio: Gpio43,
    din_gpio: Gpio46,
) -> Result<I2sDriver<'a, I2sRx>, anyhow::Error> {
    let rx_cfg = PdmRxConfig::new(
        Config::default(),
        PdmRxClkConfig::from_sample_rate_hz(96000),
        PdmRxSlotConfig::from_bits_per_sample_and_slot_mode(DataBitWidth::Bits16, SlotMode::Mono),
        PdmRxGpioConfig::new(true),
    );

    let i2s_dvr = I2sDriver::new_pdm_rx(i2s0, &rx_cfg, clk_gpio, din_gpio)?;

    Ok(i2s_dvr)
}

fn average_and_standard_deviation(data: &[u16]) -> (f64, f64) {
    let n = data.len() as f64;
    let sum: f64 = data.iter().map(|x| *x as f64).sum();
    let mean = sum / n;

    let squared_diff_sum: f64 = data.iter().map(|x| (*x as f64 - mean).powi(2)).sum();
    let variance = squared_diff_sum / n;
    let standard_deviation = variance.sqrt();

    (mean, standard_deviation)
}

struct SampleSet {
    id: u32,
    samples: [u16; 4092],
    sample_count: usize,
}

static mut I2S_BUFFER: [u8; 4092 * 2] = [0u8; 4092 * 2];

fn i2s_task(
    i2s_rx: &mut I2sDriver<I2sRx>,
    sender: Sender<Arc<Mutex<SampleSet>>>,
    recycler: Receiver<Arc<Mutex<SampleSet>>>,
) -> Result<(), anyhow::Error> {
    println!("Begin Read");

    loop {
        let bytes_read = i2s_rx.read(unsafe { &mut I2S_BUFFER }, 1000u32)?;

        if bytes_read > 0 {
            let (head, samples, tail) = unsafe { I2S_BUFFER.align_to::<u16>() };

            assert!(head.is_empty());
            assert!(tail.is_empty());

            let sample_set_arc = recycler.recv()?;

            {
                let mut sample_set = sample_set_arc.lock().unwrap();

                println!("Received {} into {}", &bytes_read, &sample_set.id);

                sample_set.sample_count = samples.len();
                sample_set.samples.copy_from_slice(samples);
            }

            sender.send(sample_set_arc)?;
        }
    }
}

// fn consumer_task(receiver: Receiver<&'static mut SampleSet>, recycler: Sender<&'static mut SampleSet>) -> Result<(), anyhow::Error> {
//     loop {
//         let sample_set = receiver.recv()?;

//         // Process the buffer
//         let (mean, standard_deviation) = average_and_standard_deviation(&sample_set.samples);

//         println!("Mean: {}, Standard Deviation: {}", mean, standard_deviation);

//         recycler.send(sample_set)?;
//     }
// }

fn main() -> Result<()> {
    println!("Hello, world!");

    std::env::set_var("RUST_BACKTRACE", "1");

    // It is necessary to call this function once. Otherwise some patches to the runtime
    // implemented by esp-idf-sys might not link properly. See https://github.com/esp-rs/esp-idf-template/issues/71
    esp_idf_svc::sys::link_patches();

    // Bind the log crate to the ESP Logging facilities
    esp_idf_svc::log::EspLogger::initialize_default();

    let peripherals = Peripherals::take().unwrap();

    let io = peripherals.pins;

    let mut i2s_rx = configure_i2s(peripherals.i2s0, io.gpio43, io.gpio46)?;

    i2s_rx.rx_enable()?;

    println!("I2S Configured");

    let (sample_sender, sample_receiver) = mpsc::channel();
    // let (recycler_sender, recycler_receiver) = mpsc::channel();

    for i in 0..2 {
        let sample_set = Arc::new(Mutex::new(SampleSet {
            id: i,
            samples: [0u16; 4092],
            sample_count: 0,
        }));

        sample_sender.send(sample_set)?;
    }

    println!("Channels Created");

    let i2s_thread = thread::spawn(move || {
        i2s_task(&mut i2s_rx, sample_sender, sample_receiver).unwrap();
    });

    // // Create the consumer thread
    // let consumer_thread = thread::spawn(move || {
    //     consumer_task(sample_receiver, recycler_sender).unwrap();
    // });

    println!("Threads Created");

    i2s_thread.join().unwrap();
    // consumer_thread.join().unwrap();

    println!("Threads Completed");

    Ok(())
}
