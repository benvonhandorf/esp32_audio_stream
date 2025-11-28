use anyhow::Ok;

use esp_idf_svc::fs::fatfs::Fatfs;
use esp_idf_svc::fs::fatfs::MountedFatfs;
use esp_idf_svc::hal::peripheral::Peripheral;
use esp_idf_svc::hal::sd::SdCardDriver;
use esp_idf_svc::hal::sd::spi::SdSpiHostDriver;
use esp_idf_svc::hal::spi::SpiAnyPins;
use esp_idf_svc::hal::gpio::OutputPin;
use esp_idf_svc::hal::gpio::InputPin;

use esp_idf_svc::hal::spi::SpiDriver;
use esp_idf_svc::sys::EspError;

use esp_idf_svc::
    hal::
        spi::{config::DriverConfig, Dma}
    
;
use std::{fs::File, io::Write};

const _HOST_FLAG_SPI: u32 = 1 << 3;
const _HOST_FLAG_DEINIT_ARG: u32 = 1 << 5;
const _DEFAULT_FREQUENCY: i32 = 20000;
const _HIGH_SPEED_FREQUENCY: i32 = 40000;

const _DEFAULT_IO_VOLTAGE: f32 = 3.3;
const _SDMMC_HOST_FLAG_1BIT: u32 = 1 << 0;
const _SDMMC_HOST_FLAG_4BIT: u32 = 1 << 1;
const _SDMMC_HOST_FLAG_8BIT: u32 = 1 << 2;
const _SDMMC_HOST_FLAG_DDR: u32 = 1 << 3;

extern "C" fn spi_notify(transaction: *mut esp_idf_svc::sys::spi_transaction_t) {
    if let Some(transaction) = unsafe { transaction.as_ref() } {
        if let Some(notification) = unsafe {
            (transaction.user as *mut esp_idf_svc::hal::interrupt::asynch::HalIsrNotification as *const esp_idf_svc::hal::interrupt::asynch::HalIsrNotification).as_ref()
        } {
            notification.notify_lsb();
        }
    }
}

static MOUNTED_FATFS: Option<MountedFatfs<Fatfs<SdCardDriver<SdSpiHostDriver<SpiDriver>>>>> = Option::None;

pub fn configure_sdcard(
    spi: impl Peripheral<P = impl SpiAnyPins>,
    sclk: impl Peripheral<P = impl OutputPin>,
    sdo: impl Peripheral<P = impl OutputPin>,
    sdi: impl Peripheral<P = impl InputPin>,
    cs: impl Peripheral<P = impl OutputPin>,
) -> Result<(), anyhow::Error>{

    // let spi_host_id = 2;

    let mount_path = "/sdcard";

    let spi_driver = SpiDriver::new(
        peripherals.spi3,
        pins.gpio18,
        pins.gpio23,
        Some(pins.gpio19),
        &DriverConfig::default().dma(Dma::Auto(4096)),
    )?;

    let sd_card_driver = SdCardDriver::new_spi(
        SdSpiHostDriver::new(
            spi_driver,
            Some(pins.gpio5),
            AnyIOPin::none(),
            AnyIOPin::none(),
            AnyIOPin::none(),
            #[cfg(not(any(
                esp_idf_version_major = "4",
                all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
                all(esp_idf_version_major = "5", esp_idf_version_minor = "1"),
            )))] // For ESP-IDF v5.2 and later
            None,
        )?,
        &SdCardConfiguration::new(),
    )?;

    // Keep it around or else it will be dropped and unmounted
    // MOUNTED_FATFS = Option::Some(MountedFatfs::mount(Fatfs::new_sdcard(0, sd_card_driver)?, "/sdcard", 4)?);

    println!("Mounted");

    let entries = std::fs::read_dir("/sdcard/")?;

    println!("Files in root directory:");

    for entry in entries {
        let entry = entry?;
        println!("{:?} {:?}", entry.file_name(), entry.file_type()?);
    }

    Ok(())
}