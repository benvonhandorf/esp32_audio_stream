use esp_idf_svc::ipv4::IpInfo;
use esp_idf_svc::wifi::{AuthMethod, BlockingWifi, ClientConfiguration, Configuration, EspWifi};

use log::info;

mod wifi_credentials;

static mut WIFI: Option<BlockingWifi<EspWifi<'static>>> = None;

pub fn connect_wifi(mut wifi: BlockingWifi<EspWifi<'static>>) -> anyhow::Result<()> {
    unsafe {
        let wifi_configuration: Configuration = Configuration::Client(ClientConfiguration {
            ssid: wifi_credentials::SSID.try_into().unwrap(),
            bssid: None,
            auth_method: AuthMethod::WPA2Personal,
            password: wifi_credentials::PASSWORD.try_into().unwrap(),
            channel: None,
            ..Default::default()
        });

        wifi.set_configuration(&wifi_configuration)?;

        wifi.start()?;
        info!("Wifi started");

        wifi.connect()?;

        wifi.wait_netif_up()?;

        WIFI = Some(wifi);
    }
    Ok(())
}

unsafe fn get_wifi() -> Option<&'static mut BlockingWifi<EspWifi<'static>>> {
    WIFI.as_mut()
}

// Example usage in another part of your code
pub fn is_connected() -> anyhow::Result<bool> {
    unsafe {
        if let Some(wifi) = get_wifi() {
            Ok(wifi.is_connected().unwrap_or(false))
        } else {
            Ok(false)
        } 
    }
}

pub fn get_ip_info() -> anyhow::Result<Option<IpInfo>> {
    unsafe {
        if let Some(wifi) = get_wifi() {
            let ip_info = wifi.wifi().sta_netif().get_ip_info()?;
            Ok(Some(ip_info))
        } else {
            Ok(None)
        } 
    }
}
