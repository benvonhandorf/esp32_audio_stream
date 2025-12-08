/*
 * Network Module - TCP Client for Audio Streaming
 * Handles server resolution, connection, and data transmission
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"

// Network Configuration
#define MAX_SSID_LEN            32
#define MAX_PASSWORD_LEN        64
#define MAX_SERVER_ADDR_LEN     64
#define DEFAULT_SERVER_PORT     8888

// 10 minute cache time for server information
#define SERVER_CACHE_TICKS pdMS_TO_TICKS(600000)

// Network Configuration Structure
typedef struct {
    char wifi_ssid[MAX_SSID_LEN];
    char wifi_password[MAX_PASSWORD_LEN];
    char server_addr[MAX_SERVER_ADDR_LEN];
    uint16_t server_port;
    bool tcp_enabled;
} network_config_t;

// Network Context Structure
typedef struct {
    // WiFi state
    bool wifi_connected;

    // TCP state
    int tcp_socket;
    
    struct addrinfo *server_addr;
    bool server_addr_set;
    uint32_t last_resolution_ticks;

    // Configuration
    network_config_t config;
} network_context_t;

// Function declarations

/**
 * @brief Initialize WiFi stack and connect to configured network
 *
 * @param ctx Network context
 * @return esp_err_t ESP_OK on success
 */
esp_err_t network_wifi_init(network_context_t *ctx);

/**
 * @brief Initialize TCP client (currently no-op, kept for API consistency)
 *
 * @param ctx Network context
 * @return esp_err_t ESP_OK
 */
esp_err_t network_tcp_client_init(network_context_t *ctx);

/**
 * @brief Resolve server hostname/IP address to sockaddr
 *
 * @param ctx Network context
 * @return esp_err_t ESP_OK on success, ESP_FAIL on resolution failure
 */
esp_err_t network_tcp_client_resolve(network_context_t *ctx);

/**
 * @brief Connect to the resolved TCP server
 *
 * @param ctx Network context
 * @return esp_err_t ESP_OK on success, ESP_FAIL on connection failure
 */
esp_err_t network_tcp_client_connect(network_context_t *ctx);

/**
 * @brief Close the TCP connection
 *
 * @param ctx Network context
 */
void network_tcp_client_close(network_context_t *ctx);

/**
 * @brief Send data over the TCP connection
 *
 * @param ctx Network context
 * @param data Data buffer to send
 * @param size Size of data in bytes
 * @return int Number of bytes sent, or -1 on error
 */
int network_tcp_send(network_context_t *ctx, const void *data, size_t size);

/**
 * @brief Check if server resolution cache is stale and needs refresh
 *
 * @param ctx Network context
 * @return true if resolution should be attempted
 */
bool network_should_resolve(network_context_t *ctx);

#ifdef __cplusplus
}
#endif
