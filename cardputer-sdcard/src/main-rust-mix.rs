use std::io::Read;

fn main() -> anyhow::Result<()> {
    std::env::set_var("RUST_BACKTRACE", "1");

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

    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();

    let peripherals = Peripherals::take()?;
    let pins = peripherals.pins;

    let spi_driver = SpiDriver::new(
        peripherals.spi3,
        pins.gpio40,
        pins.gpio14,
        Some(pins.gpio39),
        &DriverConfig::default().dma(Dma::Auto(1024)),
    )?;

    let spi_device = SpiDevice::new(
        spi_driver,
        pins.gpio12,
        Option::<gpio::AnyInputPin>::None,
        Option::<gpio::AnyInputPin>::None,
        Option::<gpio::AnyInputPin>::None,
        Option::<bool>::None,
    );

    let mount_path = "/sdcard";

    // let spi_driver = SpiDriver::new(
    //     peripherals.spi3,
    //     pins.gpio7,
    //     pins.gpio9,
    //     Some(pins.gpio8),
    //     &DriverConfig::default().dma(Dma::Auto(1024)),
    // )?;

    // let spi_device = SpiDevice::new(
    //     spi_driver,
    //     pins.gpio21,
    //     Option::<gpio::AnyInputPin>::None,
    //     Option::<gpio::AnyInputPin>::None,
    //     Option::<gpio::AnyInputPin>::None,
    //     Option::<bool>::None,
    // );

    // #[cfg(not(any(
    //     esp_idf_version_major = "4",
    //     all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
    //     all(esp_idf_version_major = "5", esp_idf_version_minor = "1"),
    // )))] // For ESP-IDF v5.2 and later

    let host_config = SdConfiguration::new();

    let host = SdHost::new_with_spi(&host_config, spi_device);

    let mut fat_configuration = FatConfiguration::new();
    fat_configuration.format_if_mount_failed = true;

    println!("Mounting FAT filesystem...");

    let mut card: *mut esp_idf_svc::sys::sdmmc_card_t = core::ptr::null_mut();

    let base_path = std::ffi::CString::new(mount_path).unwrap();

    let config: esp_idf_svc::sys::esp_vfs_fat_mount_config_t = fat_configuration.into();
    
    unsafe {   
        let sd_mmc_host: esp_idf_svc::sys::sdmmc_host_t = 

        esp_idf_svc::sys::esp_vfs_fat_sdspi_mount(
            base_path.as_ptr(),
            host.get_inner_handle() as *const esp_idf_svc::sys::sdmmc_host_t,
            spi_device.get_device_configuration() as *const esp_idf_svc::sys::sdspi_device_config_t,
            &config as *const esp_idf_svc::sys::esp_vfs_fat_mount_config_t,
            &mut card as *mut *mut esp_idf_svc::sys::sdmmc_card_t,
        );
    }
    
    // let _fat = Fat::mount(fat_configuration, host, "/sdcard")?;

    println!("Mounted");

    {
        let mut file = File::create("/sdcard/test.txt")?;

        file.write_all(b"Hello, world!")?;

        file.flush()?;
    }

    println!("File written!");


    while(true) {

    }
    // {
    //     let mut file = File::open("/sdcard/test.txt")?;
    //     let mut contents = String::new();

    //     file.read_to_string(&mut contents)?;

    //     println!("File contents: {}", contents);
    // }

    Ok(())
}
