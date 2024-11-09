use anyhow::Ok;
use esp_idf_svc::{
    fs::{Fat, FatConfiguration},
    hal::{
        gpio,
        prelude::*,
        spi::{config::DriverConfig, Dma, SpiDriver},
    },
    log::EspLogger,
    sd::{host::SdHost, spi::SpiDevice, SdConfiguration},
};
use std::{fs::File, io::Write};

use std::os::unix::fs::MetadataExt;

use esp_idf_svc::hal::peripheral::Peripheral;
use esp_idf_svc::hal::spi::SpiAnyPins;
use esp_idf_svc::hal::gpio::OutputPin;
use esp_idf_svc::hal::gpio::InputPin;

pub fn configure_sdcard(
    spi: impl Peripheral<P = impl SpiAnyPins>,
    sclk: impl Peripheral<P = impl OutputPin>,
    sdo: impl Peripheral<P = impl OutputPin>,
    sdi: impl Peripheral<P = impl InputPin>,
    cs: impl Peripheral<P = impl OutputPin>,
) -> Result<(), anyhow::Error>{

    println!("Configuring SpiDriver");

    let spi_driver = SpiDriver::new(
        spi,
        sclk,
        sdo,
        Some(sdi),
        &DriverConfig::default().dma(Dma::Auto(1024)),
    )?;

    println!("Configuring SpiDevice");

    let spi_device = SpiDevice::new(
        spi_driver,
        cs,
        Option::<gpio::AnyInputPin>::None,
        Option::<gpio::AnyInputPin>::None,
        Option::<gpio::AnyInputPin>::None,
        #[cfg(not(any(
            esp_idf_version_major = "4",
            all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
            all(esp_idf_version_major = "5", esp_idf_version_minor = "1"),
        )))] // For ESP-IDF v5.2 and later
        Option::<bool>::None,
    );

    println!("Configuring SdConfiguration");

    let host_config = SdConfiguration::new();

    let host = SdHost::new_with_spi(&host_config, spi_device);

    println!("Mounting Fat");

    let fat_configuration = FatConfiguration::new();

    let _fat = Fat::mount(fat_configuration, host, "/")?;

    let mut entries = std::fs::read_dir("/")?;

    println!("Files in root directory:");

    for entry in entries {
        let entry = entry?;
        println!("{:?} {:?}", entry.file_name(), entry.file_type()?);
    }

    Ok(())
}