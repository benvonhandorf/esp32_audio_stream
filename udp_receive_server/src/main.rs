use std::net::UdpSocket;
use std::sync::mpsc::{self, Sender, Receiver};
use std::thread;
use std::time::{Duration, Instant};

fn main() -> std::io::Result<()> {
    let socket = UdpSocket::bind("0.0.0.0:5555")?;
    socket.set_nonblocking(true)?;

    let (tx, rx): (Sender<usize>, Receiver<usize>) = mpsc::channel();

    // Thread to receive data
    thread::spawn(move || {
        let mut buf = [0; 1500];
        loop {
            match socket.recv_from(&mut buf) {
                Ok((size, _)) => {
                    tx.send(size).unwrap();
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

    // Thread to calculate and print throughput
    thread::spawn(move || {
        let mut total_bytes = 0;
        let mut start_time = Instant::now();

        loop {
            let elapsed = start_time.elapsed().as_secs_f64();

            if elapsed >= 5.0 {
                let throughput = total_bytes as f64 / elapsed;
                println!("Throughput: {:.2} bytes/sec", throughput);

                // Reset counters
                total_bytes = 0;
                start_time = Instant::now();
            }

            match rx.recv_timeout(Duration::from_secs(5)) {
                Ok(size) => {
                    total_bytes += size;
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
