// mcli_esp32_wifi_sta.h
// ESP32 WiFi I/O adapter for MCLI - Single client version
// Handles WiFi connection and accepts ONE client at a time

#pragma once

#include "mcli.h"

// ESP32/FreeRTOS includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * ESP32 WiFi I/O adapter - Single client version
 * Connects to WiFi, waits for ONE client, provides I/O interface
 */
class ESP32WiFiIo : public mcli::CliIoInterface {
public:
    ESP32WiFiIo(const char* ssid, const char* password, int port = 23) 
        : ssid_(ssid), password_(password), port_(port),
          socket_fd_(-1), connected_(false) {

        s_wifi_event_group = xEventGroupCreate();
        ESP_LOGI("ESP32WiFiIo", "Starting WiFi...");
        connect_wifi();
    }

    // Connect to WiFi and wait for a client (blocking)
    bool wait_for_client() {
    
        ESP_LOGI("ESP32WiFiIo", "Waiting for client on port %d...", port_);
        return accept_client();
    }

    // Get the client socket (for context creation)
    int get_client_socket() const { return socket_fd_; }

    // CliIoInterface implementation
    bool is_connected() const {
        return connected_;
    }

    void put_byte(char c) override {
        put_bytes(&c, 1);
    }

    char get_byte() override {
        if (!connected_ || socket_fd_ < 0) return 0;
        char c;
        return (get_bytes(&c, 1) == 1) ? c : 0;
    }

    bool byte_available() override {
        if (!connected_ || socket_fd_ < 0) return false;
        
        char temp;
        int result = recv(socket_fd_, &temp, 1, MSG_PEEK);
        if (result <= 0) {
            if (result == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                connected_ = false;
            }
            return false;
        }
        return true;
    }

    size_t get_bytes(char* buffer, size_t max_len) override {
        if (!connected_ || socket_fd_ < 0) return 0;
        
        int result = recv(socket_fd_, buffer, max_len, 0);
        if (result <= 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                connected_ = false;
            }
            return 0;
        } else if (result == 0) {
            connected_ = false;
            return 0;
        }

        // Filter out telnet commands in-place
        size_t write_pos = 0;
        for (int read_pos = 0; read_pos < result; read_pos++) {
            unsigned char c = buffer[read_pos];

            // Handle telnet data
            if (c == 0xFF) {
                // Skip telnet command sequence (usually 3 bytes)
                if (read_pos + 2 < result) {
                    read_pos += 2; // Skip the next 2 bytes
                } else {
                    // Command sequence incomplete, skip rest
                    break;
                }
            } else {
                // Keep regular character, compact the buffer
                buffer[write_pos++] = c;
            }
        }
        
        return write_pos;
    }

    void put_bytes(const char* data, size_t len) override {
        if (!connected_ || socket_fd_ < 0) return;

        const size_t CHUNK_SIZE = 32; // Send 32 bytes at a time
        size_t offset = 0;

        while (offset < len) {
            size_t chunk_len = (len - offset < CHUNK_SIZE) ? (len - offset) : CHUNK_SIZE;

            int result = send(socket_fd_, data + offset, chunk_len, 0);

            if (result < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Brief pause and retry this chunk
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                } else {
                    ESP_LOGE("TCP", "send failed: errno=%d (%s)", errno, strerror(errno));
                    connected_ = false;
                    return;
                }
            } else if (result == 0) {
                ESP_LOGW("TCP", "send returned 0");
                break;
            } else {
                offset += result;
                // Small delay between chunks to avoid overwhelming the buffer
                if (offset < len) {
                    vTaskDelay(pdMS_TO_TICKS(1)); 
                }
            }
        }
    }

private:
    const char* ssid_;
    const char* password_;
    int port_;
    EventGroupHandle_t s_wifi_event_group;
    
    int socket_fd_;
    bool connected_;

    void send_telnet_response(char byte1, char byte2) {
        put_byte(0xFF);
        put_byte(byte1);
        put_byte(byte2);
    }

    // WiFi event handler
    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        ESP32WiFiIo* adapter = static_cast<ESP32WiFiIo*>(arg);
        
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI("WiFi", "Disconnected, retrying...");
            esp_wifi_connect();
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI("WiFi", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(adapter->s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }

    void connect_wifi() {
        // Initialize NVS
        ESP_ERROR_CHECK(nvs_flash_init());
        
        // Initialize network interface
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        // Initialize WiFi
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        // Register event handlers
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, this));

        // Configure WiFi
        wifi_config_t wifi_config = {};
        strcpy((char*)wifi_config.sta.ssid, ssid_);
        strcpy((char*)wifi_config.sta.password, password_);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        // Wait for connection
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI("WiFi", "Connected to %s", ssid_);
        } else {
            ESP_LOGE("WiFi", "Failed to connect to %s", ssid_);
        }
    }
    

    bool accept_client() {

        // Close any exisiting socket_fd_ if open!
        if (socket_fd_ >= 0) {
            ESP_LOGI("TCP", "Closing existing socket %d", socket_fd_);
            close(socket_fd_);
            socket_fd_ = -1;
        }
        
        // Reset connection state
        connected_ = false;

        // Create socket
        int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock < 0) {
            ESP_LOGE("TCP", "Failed to create socket");
            return false;
        }

        // Enable address reuse
        int reuse = 1;
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            ESP_LOGW("TCP", "Failed to set SO_REUSEADDR: errno=%d", errno);
        }
        
        // Bind to port
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            ESP_LOGE("TCP", "Failed to bind to port %d", port_);
            close(listen_sock);
            return false;
        }
        
        // Listen
        if (listen(listen_sock, 1) < 0) {
            ESP_LOGE("TCP", "Failed to listen");
            close(listen_sock);
            return false;
        }
        
        ESP_LOGI("TCP", "Listening on port %d", port_);
        
        // Accept ONE client
        socket_fd_ = accept(listen_sock, NULL, NULL);

        // Close listening socket immediately after accepting
        close(listen_sock);
        
        if (socket_fd_ < 0) {
            ESP_LOGE("TCP", "Failed to accept client: errno=%d", errno);
            return false;
        }
        
        // Make socket non-blocking
        int flags = fcntl(socket_fd_, F_GETFL, 0);
        if (flags < 0) {
            ESP_LOGE("TCP", "Failed to get socket flags: errno=%d", errno);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            ESP_LOGE("TCP", "Failed to set non-blocking: errno=%d", errno);
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }

        int nodelay = 1;
        if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
            ESP_LOGW("TCP", "Failed to set TCP_NODELAY: errno=%d", errno);
        }

        connected_ = true;
        ESP_LOGI("TCP", "Client connected on socket %d", socket_fd_);

        // Do some telnet setup
        send_telnet_response(0xFB, 0x01); // WILL echo
        send_telnet_response(0xFB, 0x03); // WILL suppress go ahead

        return true;
    }

};