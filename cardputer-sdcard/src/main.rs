use std::io::Read;

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

    let spi_host_id = 3;

    let cs = 12;
    let mosi = 14;
    let miso = 39;
    let sclk = 40;

    unsafe {
        let driver_config = DriverConfig::default().dma(Dma::Auto(1024));
        let max_transfer_sz = driver_config.dma.max_transfer_size();

        let spi_bus_config: esp_idf_svc::sys::spi_bus_config_t =
            esp_idf_svc::sys::spi_bus_config_t {
                flags: esp_idf_svc::sys::SPICOMMON_BUSFLAG_MASTER,
                sclk_io_num: sclk,
                data4_io_num: -1,
                data5_io_num: -1,
                data6_io_num: -1,
                data7_io_num: -1,
                __bindgen_anon_1: esp_idf_svc::sys::spi_bus_config_t__bindgen_ty_1 {
                    mosi_io_num: mosi,
                    //data0_io_num: -1,
                },
                __bindgen_anon_2: esp_idf_svc::sys::spi_bus_config_t__bindgen_ty_2 {
                    miso_io_num: miso,
                    //data1_io_num: -1,
                },
                __bindgen_anon_3: esp_idf_svc::sys::spi_bus_config_t__bindgen_ty_3 {
                    quadwp_io_num: -1,
                    //data2_io_num: -1,
                },
                __bindgen_anon_4: esp_idf_svc::sys::spi_bus_config_t__bindgen_ty_4 {
                    quadhd_io_num: -1,
                    //data3_io_num: -1,
                },
                max_transfer_sz: max_transfer_sz as i32,
                intr_flags: 0,
                ..Default::default()
            };

        let r = esp_idf_svc::sys::spi_bus_initialize(
            spi_host_id,
            &spi_bus_config as *const esp_idf_svc::sys::spi_bus_config_t,
            esp_idf_svc::sys::spi_dma_chan_t::from(driver_config.dma),
        );

        if(r != esp_idf_svc::sys::ESP_OK) {
            panic!("Failed to initialize SPI bus: {r}");
        }

        let device_interface_config = esp_idf_svc::sys::spi_device_interface_config_t {
            spics_io_num: cs,
            clock_speed_hz: 1_000_000,
            mode: 0_u8,
            queue_size: 1 as i32,
            flags: 0,
            cs_ena_pretrans: 0,
            cs_ena_posttrans: 0,
            post_cb: Some(spi_notify),
            ..Default::default()
        };

        let device= esp_idf_svc::sys::spi_device_t {
        };

        let r = esp_idf_svc::sys::spi_bus_add_device(spi_host_id, &device_interface_config, &mut device);

        if(r != esp_idf_svc::sys::ESP_OK) {
            panic!("Failed to add device to bus: {r}");
        }

        let mount_path = "/sdcard";

        let spi_host = sd_mmc_host_t {
            flags: _HOST_FLAG_SPI | _HOST_FLAG_DEINIT_ARG,
            slot: spi_host_id as i32,
            max_freq_khz: 20000,
            io_voltage: 3.3,
            init: Some(esp_idf_svc::sys::sdspi_host_init),
            set_bus_width: None,
            get_bus_width: None,
            set_bus_ddr_mode: None,
            set_card_clk: Some(esp_idf_svc::sys::sdspi_host_set_card_clk),
            set_cclk_always_on: None,
            do_transaction: Some(esp_idf_svc::sys::sdspi_host_do_transaction),
            __bindgen_anon_1: esp_idf_svc::sys::sdmmc_host_t__bindgen_ty_1 {
                deinit_p: Some(esp_idf_svc::sys::sdspi_host_remove_device),
            },
            io_int_enable: Some(esp_idf_svc::sys::sdspi_host_io_int_enable),
            io_int_wait: Some(esp_idf_svc::sys::sdspi_host_io_int_wait),
            #[cfg(not(any(
                esp_idf_version_major = "4",
                all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
            )))]    // For ESP-IDF v5.1 and later
            get_real_freq: Some(esp_idf_svc::sys::sdspi_host_get_real_freq),
            #[cfg(not(any(
                esp_idf_version_major = "4",
                all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
                all(esp_idf_version_major = "5", esp_idf_version_minor = "1"),
            )))]    // For ESP-IDF v5.2 and later
            input_delay_phase: esp_idf_svc::sys::sdmmc_delay_phase_t_SDMMC_DELAY_PHASE_0,
            #[cfg(not(any(
                esp_idf_version_major = "4",
                all(esp_idf_version_major = "5", esp_idf_version_minor = "0"),
                all(esp_idf_version_major = "5", esp_idf_version_minor = "1"),
            )))]   // For ESP-IDF v5.2 and later
            set_input_delay: None,
            command_timeout_ms: 0,
        };

        println!("Mounting FAT filesystem...");

        let mut card: *mut esp_idf_svc::sys::sdmmc_card_t = core::ptr::null_mut();

        let base_path = std::ffi::CString::new(mount_path).unwrap();

        let config: esp_idf_svc::sys::esp_vfs_fat_mount_config_t = fat_configuration.into();

        let sd_mmc_host: esp_idf_svc::sys::sdmmc_host_t = esp_idf_svc::sys::esp_vfs_fat_sdspi_mount(
            base_path.as_ptr(),
            host_config,
            spi_device.get_de
            vice_configuration() as *const esp_idf_svc::sys::sdspi_device_config_t,
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

    while (true) {}
    // {
    //     let mut file = File::open("/sdcard/test.txt")?;
    //     let mut contents = String::new();

    //     file.read_to_string(&mut contents)?;

    //     println!("File contents: {}", contents);
    // }

    Ok(())
}
