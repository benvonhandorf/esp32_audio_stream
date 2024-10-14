
## 2024-10-14

Goals:
- [ ] Debug audio issue
- [ ] Wifi stability test

## 2024-10-13

- Arc and Box both worked for Channel communication
- Implemented partition table
- Wifi UDP throughput test - ~192 kB/s from the I2S data source, full input data
- Sample data is not legible, with broad spectrum noise
  - Some format issue?  Should be i16?

## 2024-10-12

- I2S PDM configuration
- Threading setup
- Channel configuration
  - Strugging with types of data to pass through the channels for efficiency