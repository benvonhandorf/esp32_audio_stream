use std::io::Write;
use std::net::UdpSocket;
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use std::time::{Duration, Instant};
use std::fs::{self, File, OpenOptions};

const BUFFER_SIZE: usize = 65535;

type SampleFormat = i16;

#[allow(dead_code)]
fn average_and_standard_deviation(data: &Vec<SampleFormat>) -> (f64, f64) {
    let n = data.len() as f64;
    let sum: f64 = data.iter().map(|x| *x as f64).sum();
    let mean = sum / n;

    let squared_diff_sum: f64 = data.iter().map(|x| (*x as f64 - mean).powi(2)).sum();
    let variance = squared_diff_sum / n;
    let standard_deviation = variance.sqrt();

    (mean, standard_deviation)
}

#[allow(dead_code)]
fn convert_network_to_host_samples(data: Vec<u8>, byte_count: usize) -> Vec<SampleFormat> {
    let mut result = Vec::with_capacity(byte_count / 2);

    for chunk in data.chunks_exact(2) {
        let sample_value = SampleFormat::from_be_bytes([chunk[0], chunk[1]]);
        result.push(sample_value);
    }

    result
}

trait ToLittleEndianBytes {
    fn to_le_bytes(&self) -> Vec<u8>;
}

impl ToLittleEndianBytes for Vec<SampleFormat> {
    fn to_le_bytes(&self) -> Vec<u8> {
        let mut result = Vec::new();

        for &value in self {
            result.extend(value.to_le_bytes());
        }

        result
    }
}

struct Data {
    data: Vec<u8>,
    size: usize,
}

fn main() -> std::io::Result<()> {
    let socket = UdpSocket::bind("0.0.0.0:5555")?;
    socket.set_nonblocking(true)?;

    let (tx, rx): (Sender<Data>, Receiver<Data>) = mpsc::channel();

    // Thread to receive data
    thread::spawn(move || {
        loop {
            let mut buf: [u8; BUFFER_SIZE] = [0; BUFFER_SIZE];

            match socket.recv_from(&mut buf) {
                Ok((size, _)) => {
                    let data = Data {
                        data: buf[0..size].to_vec(),
                        size,
                    };

                    tx.send(data).unwrap();
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                    // No data received, continue
                    thread::sleep(Duration::from_millis(10));
                }
                Err(e) => {
                    eprintln!("Error receiving data: {}", e);
                    break;
                }
            }
        }
    });

    // Thread to calculate and print throughput and write output data
    thread::spawn(move || {
        let data_filename = "output.raw";
        let mut options = OpenOptions::new();

        options.write(true)
                .create(true);

        let mut data_file = options.open(data_filename).unwrap();

        let mut total_bytes: u32 = 0;
        let mut packets: u32 = 0;
        let mut start_time = Instant::now();
        let mut avg: f64 = 0.0;
        let mut stdev: f64 = 0.0;

        loop {
            let elapsed = start_time.elapsed().as_secs_f64();

            if elapsed >= 5.0 {
                if total_bytes > 0 {
                    let throughput = total_bytes as f64 / elapsed;
                    let bytes_per_packet = total_bytes / packets;
                    println!(
                        "Throughput: {:.2} bytes/sec {:.2} bytes / packet {:?} packets {:?} bytes - {:?} {:?}",
                        throughput, bytes_per_packet, packets, total_bytes, avg, stdev
                    );
                }

                // Reset counters
                total_bytes = 0;
                packets = 0;
                start_time = Instant::now();
            }

            match rx.recv_timeout(Duration::from_secs(5)) {
                Ok(data) => {
                    total_bytes += data.size as u32;
                    packets += 1;

                    let sample_data = convert_network_to_host_samples(data.data, data.size);

                    let le_bytes = sample_data.to_le_bytes();

                    data_file.write_all(&le_bytes).unwrap();

                    (avg, stdev) = average_and_standard_deviation(&sample_data);

                }
                Err(mpsc::RecvTimeoutError::Timeout) => {
                    //Timeout, continue
                }
                Err(e) => {
                    eprintln!("Error receiving data size: {}", e);
                    break;
                }
            }
        }
    });

    // Keep the main thread alive
    loop {
        thread::sleep(Duration::from_secs(60));
    }
}
