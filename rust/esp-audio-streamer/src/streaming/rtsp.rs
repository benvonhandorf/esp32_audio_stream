use esp_idf_sys as _;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};
use std::thread;

use esp_idf_hal::prelude::*;
use esp_idf_svc::netif::*;
use esp_idf_svc::nvs::*;
use esp_idf_svc::sysloop::*;
use esp_idf_svc::wifi::*;

struct RtspServerConfig {
    port: u16,
    max_clients: usize,
}

struct RtspServer {
    config: RtspServerConfig,
    clients: Arc<Mutex<HashMap<String, TcpStream>>>,
}

impl RtspServer {
    fn new(config: RtspServerConfig) -> Self {
        RtspServer {
            config,
            clients: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    fn start(&self) {
        let listener = TcpListener::bind(("0.0.0.0", self.config.port)).unwrap();
        println!("RTSP Server listening on port {}", self.config.port);

        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    let clients = Arc::clone(&self.clients);
                    thread::spawn(move || {
                        RtspServer::handle_client(clients, stream);
                    });
                }
                Err(e) => {
                    eprintln!("Failed to accept client: {}", e);
                }
            }
        }
    }

    fn handle_client(clients: Arc<Mutex<HashMap<String, TcpStream>>>, mut stream: TcpStream) {
        let mut buffer = [0; 1024];
        match stream.read(&mut buffer) {
            Ok(_) => {
                let request = String::from_utf8_lossy(&buffer[..]);
                println!("Received request: {}", request);

                // Handle RTSP request here (e.g., SETUP, PLAY, TEARDOWN)

                // For simplicity, just echo the request back to the client
                stream.write_all(request.as_bytes()).unwrap();
            }
            Err(e) => {
                eprintln!("Failed to read from client: {}", e);
            }
        }

        // Add client to the session
        let client_addr = stream.peer_addr().unwrap().to_string();
        clients.lock().unwrap().insert(client_addr.clone(), stream);

        // Handle client teardown
        clients.lock().unwrap().remove(&client_addr);
    }
}
