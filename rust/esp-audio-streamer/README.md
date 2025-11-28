
## Setup

### Dependencies

- `rustup`
- [`espup`](https://github.com/esp-rs/espup)
    - Handles installation of the ESP-IDF
- [`probe-rs`](https://probe.rs/docs/getting-started/installation/)
- VS Code Extensions
    - `rust-analyzer` - VS Code Extension
    - `dependi` - Rust dependency version checks
    - `probe-rs-debug` - Probe RS Extension to allow launch.json
    - `esp-idf extension` - 

### Troubleshooting

- ESP-IDF Version
    - esp-idf-svc and dependencies are built only against specific ESP-IDF versions.
    - For command line builds, make sure the proper esp-idf is set in the environment and the correct ESP_IDF is in `.cargo/config.toml`
    - For rust-analyzer builds, ensure the correct esp-idf path is set in settings.json