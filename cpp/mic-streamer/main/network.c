/*
 * Network Module Implementation
 * TCP client for audio streaming with WiFi management
 */

#include "network.h"

#include <errno.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char* TAG = "network";

// ============================================================================
// WiFi Event Handler
// ============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    network_context_t* ctx = (network_context_t*)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ctx->wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ctx->wifi_connected = true;
    }
}

esp_err_t network_wifi_init(network_context_t* ctx) {
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Context is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ctx->config.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "WiFi SSID not configured, skipping WiFi init");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WiFi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, ctx));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, ctx));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ctx->config.wifi_ssid,
            sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, ctx->config.wifi_password,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization complete");

    return ESP_OK;
}

// ============================================================================
// TCP Client
// ============================================================================

esp_err_t network_tcp_client_init(network_context_t* ctx) {
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Context is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // No early initialization is required ahead of the attempt to resolve or
    // connect
    ctx->tcp_socket = -1;
    ctx->server_addr = NULL;
    ctx->server_addr_set = false;
    ctx->last_resolution_ticks = 0;
    ctx->wifi_connected = false;

    return ESP_OK;
}

esp_err_t network_tcp_client_resolve(network_context_t* ctx) {
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Context is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!ctx->config.tcp_enabled || strlen(ctx->config.server_addr) == 0) {
        ESP_LOGI(TAG, "TCP client disabled or server not configured");
        return ESP_OK;
    }

    ctx->last_resolution_ticks = xTaskGetTickCount();

    ESP_LOGI(TAG, "Resolving server address: %s", ctx->config.server_addr);

    // Resolve hostname or IP address using getaddrinfo
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", ctx->config.server_port);

    struct addrinfo* res = NULL;

    int err = getaddrinfo(ctx->config.server_addr, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "Failed to resolve hostname %s: errno %d",
                 ctx->config.server_addr, err);
        return ESP_FAIL;
    }

    // Free previous resolution if any
    if (ctx->server_addr_set) {
        freeaddrinfo(ctx->server_addr);
        ctx->server_addr_set = false;
    }

    ctx->server_addr = res;
    ctx->server_addr_set = true;

    // Extract resolved IP address for logging
    struct sockaddr_in* addr_in =
        (struct sockaddr_in*)ctx->server_addr->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "Resolved %s to %s:%d", ctx->config.server_addr, ip_str,
             ctx->config.server_port);

    //TODO: Attempt connect on a network task rather than the main
    // Attempt to connect to the server to see if it's really alive
    // int sock =
    //     socket(ctx->server_addr->ai_family, ctx->server_addr->ai_socktype,
    //            ctx->server_addr->ai_protocol);
    // if (sock < 0) {
    //     ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    //     return ESP_FAIL;
    // }

    // err =
    //     connect(sock, ctx->server_addr->ai_addr, ctx->server_addr->ai_addrlen);

    // if (err != 0) {
    //     ESP_LOGE(TAG, "Unable to connect to resolved addr errno=%d", errno);
    //     close(sock);

    //     // Free resolution on connection failure
    //     if (ctx->server_addr_set) {
    //         freeaddrinfo(ctx->server_addr);
    //         ctx->server_addr_set = false;
    //     }

    //     return ESP_FAIL;
    // }

    // ESP_LOGI(TAG, "Successfully verified connectivity to TCP server");
    // close(sock);

    return ESP_OK;
}

esp_err_t network_tcp_client_connect(network_context_t* ctx) {
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Context is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!ctx->config.tcp_enabled || strlen(ctx->config.server_addr) == 0 ||
        !ctx->server_addr_set || ctx->server_addr->ai_family == AF_UNSPEC) {
        ESP_LOGI(TAG,
                 "TCP client disabled, server not configured, or not resolved");
        ctx->tcp_socket = -1;
        return ESP_OK;
    }

    // Extract resolved IP address for logging
    struct sockaddr_in* addr_in =
        (struct sockaddr_in*)ctx->server_addr->ai_addr;
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));

    ESP_LOGI(TAG, "Connecting to TCP server %s:%d", ip_str,
             ctx->config.server_port);

    int sock =
        socket(ctx->server_addr->ai_family, ctx->server_addr->ai_socktype,
               ctx->server_addr->ai_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    int err =
        connect(sock, ctx->server_addr->ai_addr, ctx->server_addr->ai_addrlen);

    if (err != 0) {
        ESP_LOGE(TAG, "Socket connect failed errno=%d", errno);
        close(sock);
        ctx->tcp_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully connected to TCP server");
    ctx->tcp_socket = sock;

    return ESP_OK;
}

void network_tcp_client_close(network_context_t* ctx) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->tcp_socket >= 0) {
        close(ctx->tcp_socket);
        ctx->tcp_socket = -1;
        ESP_LOGI(TAG, "TCP connection closed");
    }
}

int network_tcp_send(network_context_t* ctx, const void* data, size_t size) {
    if (ctx == NULL || data == NULL || size == 0) {
        return -1;
    }

    if (ctx->tcp_socket < 0) {
        return -1;
    }

    int sent = send(ctx->tcp_socket, data, size, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "TCP send error: errno %d", errno);
        network_tcp_client_close(ctx);
    }

    return sent;
}

bool network_should_resolve(network_context_t* ctx) {
    if (ctx == NULL || !ctx->wifi_connected) {
        return false;
    }

    TickType_t now = xTaskGetTickCount();

    // Resolve if we've never resolved or cache is stale
    if (ctx->last_resolution_ticks == 0 ||
        now - ctx->last_resolution_ticks > SERVER_CACHE_TICKS) {
        return true;
    }

    return false;
}
