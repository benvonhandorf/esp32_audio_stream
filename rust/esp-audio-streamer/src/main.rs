use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::result::Result;
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use std::time::Duration;

use log;

use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::wifi::{BlockingWifi, EspWifi};

use esp_idf_svc::hal::{
    gpio::{Gpio43, Gpio46},
    i2s::config::*,
    i2s::I2S0,
    i2s::*,
    peripherals::Peripherals,
};

mod wifi_management;

mod sdcard;

const BUFFER_SIZE: usize = 1024;
const SAMPLE_RATE: u32 = 22050;

type SampleFormat = i16;

fn main() -> anyhow::Result<()> {
    println!("Hello, world!");

    std::env::set_var("RUST_BACKTRACE", "1");

    // It is necessary to call this function once. Otherwise some patches to the runtime
    // implemented by esp-idf-sys might not link properly. See https://github.com/esp-rs/esp-idf-template/issues/71
    esp_idf_svc::sys::link_patches();

    let peripherals = Peripherals::take().unwrap();
    let sys_loop = EspSystemEventLoop::take()?;

    let io = peripherals.pins;

    // Configure SD Card

    // sdcard::configure_sdcard(peripherals.spi2, io.gpio40, io.gpio14, io.gpio39, io.gpio12)?;

    //Configure Wifi

    let wifi = BlockingWifi::wrap(
        EspWifi::new(peripherals.modem, sys_loop.clone(), None)?,
        sys_loop,
    )?;

    wifi_management::connect_wifi(wifi)?;

    while !wifi_management::is_connected()? {
        thread::sleep(Duration::from_secs(1));
    }

    if let Some(ip_info) = wifi_management::get_ip_info()? {
        log::info!("Wifi DHCP info: {:?}", ip_info);
    };

    let mut i2s_rx = configure_i2s(peripherals.i2s0, io.gpio43, io.gpio46)?;

    i2s_rx.rx_enable()?;

    println!("I2S Configured");

    // Build the communication channels

    let (sample_sender, sample_receiver) = mpsc::channel();
    let (writer_sender, writer_receiver) = mpsc::channel();
    let (recycler_sender, recycler_receiver) = mpsc::channel();

    for i in 0..3 {
        let sample_set = Box::new(SampleSet {
            id: i,
            samples: [0; BUFFER_SIZE / 2],
            sample_count: 0,
        });

        recycler_sender.send(sample_set)?;
    }

    println!("Channels Created");

    let i2s_thread = thread::spawn(move || {
        i2s_task(&mut i2s_rx, recycler_receiver, sample_sender).unwrap();
    });

    // Create the consumer thread
    let consumer_thread = thread::spawn(move || {
        consumer_task(sample_receiver, writer_sender).unwrap();
    });

    // Create the writer thread
    let writer_thread = thread::spawn(move || {
        sd_writer_task(writer_receiver, recycler_sender).unwrap();
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
        Config::default()
            .dma_buffer_count(16)
            .frames_per_buffer(256)
            .role(Role::Controller),
        PdmRxClkConfig::from_sample_rate_hz(SAMPLE_RATE)
            .mclk_multiple(MclkMultiple::M128)
            .downsample_mode(PdmDownsample::Samples8),
        PdmRxSlotConfig::from_bits_per_sample_and_slot_mode(DataBitWidth::Bits16, SlotMode::Mono)
            .slot_bit_width(SlotBitWidth::Bits16),
        PdmRxGpioConfig::new(false),
    );

    let i2s_dvr = I2sDriver::new_pdm_rx(i2s0, &rx_cfg, clk_gpio, din_gpio)?;

    Ok(i2s_dvr)
}

#[allow(dead_code)]
struct SampleSet {
    id: u32,
    samples: [SampleFormat; BUFFER_SIZE / 2],
    sample_count: usize,
}

#[allow(dead_code)]
fn average_and_standard_deviation(data: &[SampleFormat]) -> (f64, f64) {
    let n = data.len() as f64;
    let sum: f64 = data.iter().map(|x| *x as f64).sum();
    let mean = sum / n;

    let squared_diff_sum: f64 = data.iter().map(|x| (*x as f64 - mean).powi(2)).sum();
    let variance = squared_diff_sum / n;
    let standard_deviation = variance.sqrt();

    (mean, standard_deviation)
}

struct SerializationResult {
    data: Box<[u8]>,
    size_bytes: usize,
    capacity_bytes: usize,
}
trait ToByteFormats {
    fn to_network_bytes(&self, output: &mut SerializationResult) -> anyhow::Result<()>;
    fn to_disk_bytes(&self, output: &mut SerializationResult) -> anyhow::Result<()>;
}

impl ToByteFormats for [SampleFormat] {
    fn to_network_bytes(&self, output: &mut SerializationResult) -> anyhow::Result<()> {
        for (i, &value) in self.iter().enumerate().take(output.capacity_bytes / 2) {
            let be_value = value.to_be(); // Convert to big-endian
            output.data[i * 2] = (be_value >> 8) as u8;
            output.data[i * 2 + 1] = be_value as u8;

            output.size_bytes = (i * 2) + 2;
        }

        Ok(())
    }

    fn to_disk_bytes(&self, output: &mut SerializationResult) -> anyhow::Result<()> {
        for (i, &value) in self.iter().enumerate().take(output.capacity_bytes / 2) {
            let be_value = value.to_le(); // Convert to big-endian
            output.data[i * 2] = (be_value >> 8) as u8;
            output.data[i * 2 + 1] = be_value as u8;

            output.size_bytes = (i * 2) + 2;
        }

        Ok(())
    }
}

// fn i16_to_network_bytes(input: &[i16], count: usize, output: &mut [u8]) {
//     for (i, &value) in input.iter().enumerate().take(count) {
//         let be_value = value.to_be(); // Convert to big-endian
//         output[i * 2] = (be_value >> 8) as u8;
//         output[i * 2 + 1] = be_value as u8;
//     }
// }

// fn i16_to_disk_bytes(input: &[i16], count: usize, output: &mut [u8]) {
//     for (i, &value) in input.iter().enumerate().take(count) {
//         output[i * 2] = value as u8;
//         output[i * 2 + 1] = (value >> 8) as u8;
//     }
// }

fn consumer_task(
    receiver: Receiver<Box<SampleSet>>,
    sender: Sender<Box<SampleSet>>,
) -> Result<(), anyhow::Error> {
    let udp_socket = UdpSocket::bind("0.0.0.0:0").expect("Failed to bind UDP socket");
    let server_addr = "zealot.local:5555";
    let server_addrs = server_addr.to_socket_addrs().unwrap().next().unwrap();

    if let SocketAddr::V4(socket_addr_v4) = server_addrs {
        log::info!("Target Address: {}", socket_addr_v4);
    } else {
        log::warn!("Target Address resolution failed");
    }

    let mut network_buffer: SerializationResult = SerializationResult {
        data: vec![0u8; BUFFER_SIZE].into_boxed_slice(),
        size_bytes: 0,
        capacity_bytes: BUFFER_SIZE,
    };

    loop {
        if let Ok(sample_set) = receiver.recv() {
            let mut sample_offset = 0;

            while sample_offset < sample_set.sample_count {
                let buffer_samples = BUFFER_SIZE / 2;
                let remaining_samples = sample_set.sample_count - sample_offset;
                let samples_to_copy = if remaining_samples > buffer_samples {
                    buffer_samples
                } else {
                    remaining_samples
                };

                if sample_set.samples[sample_offset..samples_to_copy]
                    .to_network_bytes(&mut network_buffer)
                    .is_ok()
                {
                    if let Err(e) = udp_socket.send_to(
                        &network_buffer.data[0..network_buffer.size_bytes],
                        server_addrs,
                    ) {
                        println!("Error sending UDP packet: {:?}", e);
                    }
                }

                sample_offset += samples_to_copy;
            }

            sender.send(sample_set)?;
        } else {
            println!("No data");
        }

        // Process the buffer
        // let (mean, standard_deviation) = average_and_standard_deviation(&sample_set.samples);

        // println!(
        //     "Received: {}, Count: {} {} {}",
        //     sample_set.id, sample_set.sample_count, mean as u32, standard_deviation as u32
        // );
    }
}

use std::fs::File;
use std::io::Write;
use std::path::Path;
use std::time::Instant;

fn sd_writer_task(
    receiver: Receiver<Box<SampleSet>>,
    sender: Sender<Box<SampleSet>>,
) -> Result<(), anyhow::Error> {
    let path = Path::new("/sdcard/recorded.raw");

    let mut disk_buffer: SerializationResult = SerializationResult {
        data: vec![0u8; BUFFER_SIZE].into_boxed_slice(),
        size_bytes: 0,
        capacity_bytes: BUFFER_SIZE,
    };

    // Open a file in write-only mode, returns `io::Result<File>`
    let mut file = match File::create(&path) {
        Err(e) => panic!("couldn't create {:?}: {}", path, e),
        Ok(file) => file,
    };

    let mut start = Instant::now();
    let mut sample_count: usize = 0;

    loop {
        if let Ok(sample_set) = receiver.recv() {
            let mut sample_offset = 0;

            while sample_offset < sample_set.sample_count {
                let buffer_samples = BUFFER_SIZE / 2;
                let remaining_samples = sample_set.sample_count - sample_offset;
                let samples_to_copy = if remaining_samples > buffer_samples {
                    buffer_samples
                } else {
                    remaining_samples
                };

                if sample_set.samples[sample_offset..samples_to_copy]
                    .to_disk_bytes(&mut disk_buffer)
                    .is_ok()
                {
                    match file.write(&disk_buffer.data[0..disk_buffer.size_bytes]) {
                        Err(why) => panic!("couldn't write to {:?}: {}", path, why),
                        Ok(_) => {}
                    }
                }

                sample_offset += samples_to_copy;

                sample_count += samples_to_copy;
            }

            sender.send(sample_set)?;
        } else {
            println!("No data");
        }

        if sample_count > 100_000 {
            let current_instant = Instant::now();
            let elapsed = current_instant.duration_since(start);
            let rate = (sample_count as f32) / elapsed.as_secs_f32();

            log::info!(
                "Processed {:?} samples in {:?} : {:?}",
                sample_count,
                elapsed,
                rate
            );

            start = current_instant;
            sample_count = 0;

            file.flush().unwrap();
        }

        // Process the buffer
        // let (mean, standard_deviation) = average_and_standard_deviation(&sample_set.samples);

        // println!(
        //     "Received: {}, Count: {} {} {}",
        //     sample_set.id, sample_set.sample_count, mean as u32, standard_deviation as u32
        // );
    }
}

fn i2s_task(
    i2s_rx: &mut I2sDriver<I2sRx>,
    receiver: Receiver<Box<SampleSet>>,
    sender: Sender<Box<SampleSet>>,
) -> Result<(), anyhow::Error> {
    println!("Begin I2S Task");

    let mut i2s_buffer: Box<[u8]> = vec![0u8; BUFFER_SIZE].into_boxed_slice();

    loop {
        let bytes_read = i2s_rx.read(&mut i2s_buffer, 1000u32)?;

        if bytes_read > 0 {
            //PDM mode samples are stored in memory in Little Endian format already, because
            //they're generated by the ESP32.  If this were an I2S reception it would be in
            //network byte order
            let (head, samples, tail) = unsafe { i2s_buffer.align_to::<SampleFormat>() };

            assert!(head.is_empty());
            assert!(tail.is_empty());

            let mut sample_set = receiver.recv()?;

            // println!("Received {} into {}", &bytes_read, &sample_set.id);

            sample_set.sample_count = samples.len();
            sample_set.samples.copy_from_slice(samples);

            sender.send(sample_set)?;
        }
    }
}
