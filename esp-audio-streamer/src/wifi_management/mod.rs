use esp_idf_svc::wifi::{AuthMethod, BlockingWifi, ClientConfiguration, Configuration, EspWifi};
use esp_idf_svc::{eventloop::EspSystemEventLoop, nvs::EspDefaultNvsPartition};

use log::info;

mod wifi_credentials;

static mut WIFI: Option<&mut BlockingWifi<EspWifi<'static>>> = Option::None;

pub fn connect_wifi(wifi: &mut BlockingWifi<EspWifi<'static>>) -> anyhow::Result<()> {
    unsafe {
        WIFI = Some(wifi);

        let wifi_configuration: Configuration = Configuration::Client(ClientConfiguration {
            ssid: wifi_credentials::SSID.try_into().unwrap(),
            bssid: None,
            auth_method: AuthMethod::WPA2Personal,
            password: wifi_credentials::PASSWORD.try_into().unwrap(),
            channel: None,
            ..Default::default()
        });

        WIFI.unwrap().set_configuration(&wifi_configuration)?;

        WIFI.unwrap().start()?;
        info!("Wifi started");

        WIFI.unwrap().connect()?;
        info!("Wifi connected");

        WIFI.unwrap().wait_netif_up()?;
        info!("Wifi netif up");
    }

    Ok(())
}

pub fn is_connected() -> anyhow::Result<bool> {
    Ok(unsafe{ WIFI }.unwrap().is_connected()?)
}
