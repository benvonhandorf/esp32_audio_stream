use anyhow::Result;

use esp_idf_svc::hal::task;

use std::borrow::BorrowMut;
use std::net::UdpSocket;
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use std::time::Duration;

use esp_idf_svc::log::EspLogger;
use log::info;

use esp_idf_svc::wifi::{BlockingWifi, EspWifi, AuthMethod, ClientConfiguration, Configuration };
use esp_idf_svc::{eventloop::EspSystemEventLoop, nvs::EspDefaultNvsPartition};

use esp_idf_svc::hal::{
    self,
    gpio::{Gpio43, Gpio46},
    peripherals::Peripherals,
    i2s::config::*, 
    i2s::I2S0, 
    i2s::*
};

mod wifi_management;

fn main() -> Result<()> {
    println!("Hello, world!");

    std::env::set_var("RUST_BACKTRACE", "1");

    // It is necessary to call this function once. Otherwise some patches to the runtime
    // implemented by esp-idf-sys might not link properly. See https://github.com/esp-rs/esp-idf-template/issues/71
    esp_idf_svc::sys::link_patches();

    let peripherals = Peripherals::take().unwrap();
    let sys_loop = EspSystemEventLoop::take()?;

    let mut wifi = BlockingWifi::wrap(
        EspWifi::new(peripherals.modem, sys_loop.clone(), None)?,
        sys_loop,
    )?;

    wifi_management::connect_wifi(&mut wifi)?;

    while !wifi_management::is_connected()? {
        thread::sleep(Duration::from_secs(1));
    }

    let ip_info = wifi.wifi().sta_netif().get_ip_info()?;

    info!("Wifi DHCP info: {:?}", ip_info);

    let io = peripherals.pins;

    let mut i2s_rx = configure_i2s(peripherals.i2s0, io.gpio43, io.gpio46)?;

    i2s_rx.rx_enable()?;

    println!("I2S Configured");

    let (sample_sender, sample_receiver) = mpsc::channel();
    let (recycler_sender, recycler_receiver) = mpsc::channel();

    for i in 0..3 {
        let sample_set = Box::new(SampleSet {
            id: i,
            samples: [0u16; 4092],
            sample_count: 0,
        });

        recycler_sender.send(sample_set)?;
    }

    println!("Channels Created");

    let i2s_thread = thread::spawn(move || {
        i2s_task(&mut i2s_rx, sample_sender, recycler_receiver).unwrap();
    });

    // Create the consumer thread
    let consumer_thread = thread::spawn(move || {
        consumer_task(sample_receiver, recycler_sender).unwrap();
    });

    println!("Threads Created");

    i2s_thread.join().unwrap();
    consumer_thread.join().unwrap();

    println!("Threads Completed");

    Ok(())
}

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

struct SampleSet {
    id: u32,
    samples: [u16; 4092],
    sample_count: usize,
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

static mut NETWORK_BUFFER: [u8; 4092 * 2] = [0u8; 4092 * 2];

fn u16_to_bytes(input: &[u16], count: usize, output: &mut [u8]) {
    for (i, &value) in input.iter().enumerate().take(count) {
        let be_value = value.to_be(); // Convert to big-endian
        output[i * 2] = (be_value >> 8) as u8;
        output[i * 2 + 1] = be_value as u8;
    }
}

fn consumer_task(
    receiver: Receiver<Box<SampleSet>>,
    recycler: Sender<Box<SampleSet>>,
) -> Result<(), anyhow::Error> {
    let udp_socket = UdpSocket::bind("0.0.0.0:0").expect("Failed to bind UDP socket");
    let server_addr = "zealot:5555";

    loop {
        let sample_set = receiver.recv()?;

        // Process the buffer
        let (mean, standard_deviation) = average_and_standard_deviation(&sample_set.samples);

        // println!(
        //     "Received: {}, Count: {} {} {}",
        //     sample_set.id, sample_set.sample_count, mean as u32, standard_deviation as u32
        // );

        // Convert the u16 array to bytes
        unsafe {
            u16_to_bytes(
                &sample_set.samples,
                sample_set.sample_count,
                &mut NETWORK_BUFFER,
            );
        }

        udp_socket.send_to(unsafe { &NETWORK_BUFFER }, server_addr);

        recycler.send(sample_set)?;
    }
}

static mut I2S_BUFFER: [u8; 4092 * 2] = [0u8; 4092 * 2];

fn i2s_task(
    i2s_rx: &mut I2sDriver<I2sRx>,
    sender: Sender<Box<SampleSet>>,
    recycler: Receiver<Box<SampleSet>>,
) -> Result<(), anyhow::Error> {
    println!("Begin I2S Task");

    loop {
        let bytes_read = i2s_rx.read(unsafe { &mut I2S_BUFFER }, 1000u32)?;

        if bytes_read > 0 {
            let (head, samples, tail) = unsafe { I2S_BUFFER.align_to::<u16>() };

            assert!(head.is_empty());
            assert!(tail.is_empty());

            let mut sample_set = recycler.recv()?;

            // println!("Received {} into {}", &bytes_read, &sample_set.id);

            sample_set.sample_count = samples.len();
            sample_set.samples.copy_from_slice(samples);

            sender.send(sample_set)?;
        }
    }
}
