/*
 * Configuration Tool - Utility to set WiFi and server settings
 * Uses ESP Console REPL with individual commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "argtable3/argtable3.h"

static const char *TAG = "config_tool";

// Configuration storage
static struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char server_addr[64];
    uint16_t server_port;
    bool tcp_enabled;
    bool has_changes;
} config = {
    .server_port = 8888,
    .tcp_enabled = false,
    .has_changes = false
};

static void load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No existing configuration found");
        return;
    }

    size_t len;

    // Load WiFi SSID
    len = sizeof(config.wifi_ssid);
    err = nvs_get_str(nvs_handle, "wifi_ssid", config.wifi_ssid, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load wifi_ssid: %s", esp_err_to_name(err));
    }

    // Load WiFi password
    len = sizeof(config.wifi_password);
    err = nvs_get_str(nvs_handle, "wifi_pass", config.wifi_password, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load wifi_pass: %s", esp_err_to_name(err));
    }

    // Load server address
    len = sizeof(config.server_addr);
    err = nvs_get_str(nvs_handle, "server_addr", config.server_addr, &len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load server_addr: %s", esp_err_to_name(err));
    }

    // Load server port
    err = nvs_get_u16(nvs_handle, "server_port", &config.server_port);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load server_port: %s", esp_err_to_name(err));
    }

    // Load TCP enabled flag
    uint8_t tcp_enabled_u8;
    err = nvs_get_u8(nvs_handle, "tcp_enabled", &tcp_enabled_u8);
    if (err == ESP_OK) {
        config.tcp_enabled = (tcp_enabled_u8 != 0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load tcp_enabled: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Configuration loaded from NVS");
}

static void save_to_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t nvs_handle;
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return;
    }

    if (strlen(config.wifi_ssid) > 0) {
        nvs_set_str(nvs_handle, "wifi_ssid", config.wifi_ssid);
    }
    if (strlen(config.wifi_password) > 0) {
        nvs_set_str(nvs_handle, "wifi_pass", config.wifi_password);
    }
    if (strlen(config.server_addr) > 0) {
        nvs_set_str(nvs_handle, "server_addr", config.server_addr);
    }
    nvs_set_u16(nvs_handle, "server_port", config.server_port);
    nvs_set_u8(nvs_handle, "tcp_enabled", config.tcp_enabled ? 1 : 0);

    err = nvs_commit(nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved successfully");
        config.has_changes = false;
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS");
    }

    nvs_close(nvs_handle);
}

// Command: set_wifi <ssid> <password>
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} set_wifi_args;

static int cmd_set_wifi(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&set_wifi_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_wifi_args.end, argv[0]);
        return 1;
    }

    strncpy(config.wifi_ssid, set_wifi_args.ssid->sval[0], sizeof(config.wifi_ssid) - 1);
    strncpy(config.wifi_password, set_wifi_args.password->sval[0], sizeof(config.wifi_password) - 1);
    config.has_changes = true;

    printf("WiFi configured: SSID=%s\n", config.wifi_ssid);
    return 0;
}

// Command: set_server <hostname|ip> <port>
static struct {
    struct arg_str *addr;
    struct arg_int *port;
    struct arg_end *end;
} set_server_args;

static int cmd_set_server(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&set_server_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_server_args.end, argv[0]);
        return 1;
    }

    strncpy(config.server_addr, set_server_args.addr->sval[0], sizeof(config.server_addr) - 1);
    config.server_port = set_server_args.port->ival[0];
    config.has_changes = true;

    printf("Server configured: %s:%d\n", config.server_addr, config.server_port);
    return 0;
}

// Command: set_tcp <enable|disable>
static struct {
    struct arg_str *state;
    struct arg_end *end;
} set_tcp_args;

static int cmd_set_tcp(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&set_tcp_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_tcp_args.end, argv[0]);
        return 1;
    }

    const char *state = set_tcp_args.state->sval[0];
    if (strcmp(state, "enable") == 0 || strcmp(state, "1") == 0 || strcmp(state, "on") == 0) {
        config.tcp_enabled = true;
    } else {
        config.tcp_enabled = false;
    }
    config.has_changes = true;

    printf("TCP streaming: %s\n", config.tcp_enabled ? "enabled" : "disabled");
    return 0;
}

// Command: show
static int cmd_show(int argc, char **argv)
{
    printf("\n");
    printf("========================================\n");
    printf("  Current Configuration                \n");
    printf("========================================\n");
    printf("WiFi SSID:       %s\n", strlen(config.wifi_ssid) > 0 ? config.wifi_ssid : "(not set)");
    printf("WiFi Password:   %s\n", strlen(config.wifi_password) > 0 ? "********" : "(not set)");
    printf("Server Address:  %s\n", strlen(config.server_addr) > 0 ? config.server_addr : "(not set)");
    printf("Server Port:     %d\n", config.server_port);
    printf("TCP Enabled:     %s\n", config.tcp_enabled ? "Yes" : "No");
    printf("========================================\n");
    if (config.has_changes) {
        printf("* Unsaved changes - run 'save' to commit\n");
    }
    printf("\n");
    return 0;
}

// Command: save
static int cmd_save(int argc, char **argv)
{
    if (!config.has_changes) {
        printf("No changes to save\n");
        return 0;
    }

    save_to_nvs();
    printf("Configuration saved!\n");
    printf("You can now disable configuration mode and rebuild.\n");
    return 0;
}

// Command: restart
static int cmd_restart(int argc, char **argv)
{
    printf("Restarting in 3 seconds...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return 0;
}

static void register_commands(void)
{
    // set_wifi command
    set_wifi_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID");
    set_wifi_args.password = arg_str1(NULL, NULL, "<password>", "WiFi password");
    set_wifi_args.end = arg_end(2);

    const esp_console_cmd_t set_wifi_cmd = {
        .command = "set_wifi",
        .help = "Configure WiFi credentials",
        .hint = NULL,
        .func = &cmd_set_wifi,
        .argtable = &set_wifi_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_wifi_cmd));

    // set_server command
    set_server_args.addr = arg_str1(NULL, NULL, "<hostname|ip>", "Server hostname or IP address");
    set_server_args.port = arg_int0(NULL, NULL, "<port>", "Server port (default: 8888)");
    set_server_args.port->ival[0] = 8888;
    set_server_args.end = arg_end(2);

    const esp_console_cmd_t set_server_cmd = {
        .command = "set_server",
        .help = "Configure TCP server (supports hostname or IP)",
        .hint = NULL,
        .func = &cmd_set_server,
        .argtable = &set_server_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_server_cmd));

    // set_tcp command
    set_tcp_args.state = arg_str1(NULL, NULL, "<enable|disable>", "Enable or disable TCP streaming");
    set_tcp_args.end = arg_end(1);

    const esp_console_cmd_t set_tcp_cmd = {
        .command = "set_tcp",
        .help = "Enable or disable TCP streaming",
        .hint = NULL,
        .func = &cmd_set_tcp,
        .argtable = &set_tcp_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_tcp_cmd));

    // show command
    const esp_console_cmd_t show_cmd = {
        .command = "show",
        .help = "Show current configuration",
        .hint = NULL,
        .func = &cmd_show,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&show_cmd));

    // save command
    const esp_console_cmd_t save_cmd = {
        .command = "save",
        .help = "Save configuration to NVS",
        .hint = NULL,
        .func = &cmd_save,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&save_cmd));

    // restart command
    const esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the device",
        .hint = NULL,
        .func = &cmd_restart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));
}

void config_tool_run(void)
{
    ESP_LOGI(TAG, "Starting configuration tool");

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Load existing configuration
    load_from_nvs();

    // Initialize console
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "config> ";
    repl_config.max_cmdline_length = 256;

    // Configure to use USB Serial/JTAG
    esp_console_dev_usb_serial_jtag_config_t usb_serial_jtag_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    ESP_LOGI(TAG, "Initializing USB Serial/JTAG console");
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb_serial_jtag_config, &repl_config, &repl));

    // Register commands
    register_commands();

    // Print welcome message
    printf("\n\n");
    printf("========================================\n");
    printf("  ESP32 Audio Streamer Configuration   \n");
    printf("========================================\n");
    printf("\n");
    printf("Available commands:\n");
    printf("  set_wifi <ssid> <password>      - Configure WiFi\n");
    printf("  set_server <hostname|ip> <port> - Configure TCP server\n");
    printf("  set_tcp <enable|disable>        - Enable/disable TCP streaming\n");
    printf("  show                            - Show current configuration\n");
    printf("  save                            - Save configuration to NVS\n");
    printf("  restart                         - Restart device\n");
    printf("  help                            - Show all commands\n");
    printf("\n");

    // Show current configuration
    printf("\n");
    printf("========================================\n");
    printf("  Current Configuration                \n");
    printf("========================================\n");
    printf("WiFi SSID:       %s\n", strlen(config.wifi_ssid) > 0 ? config.wifi_ssid : "(not set)");
    printf("WiFi Password:   %s\n", strlen(config.wifi_password) > 0 ? "********" : "(not set)");
    printf("Server Address:  %s\n", strlen(config.server_addr) > 0 ? config.server_addr : "(not set)");
    printf("Server Port:     %d\n", config.server_port);
    printf("TCP Enabled:     %s\n", config.tcp_enabled ? "Yes" : "No");
    printf("========================================\n");
    printf("\n");
    printf("Type 'show' to view configuration, or 'help' for more information.\n");
    printf("\n");

    // Start REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
