// mcli_esp32_uart.h
// ESP32 UART I/O adapter for MCLI
#pragma once

#include "mcli.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"


class ESP32UartIo : public mcli::CliIoInterface {
    public:
        ESP32UartIo(uart_port_t uart_num = UART_NUM_0, int baud_rate = 115200, gpio_num_t tx_pin = GPIO_NUM_1, gpio_num_t rx_pin = GPIO_NUM_3)
        : uart_num_(uart_num) {
            init_uart(baud_rate, tx_pin, rx_pin);
        }

        void put_byte(char c) override {
            uart_write_bytes(uart_num_, &c, 1);
        }

        char get_byte() override {
            char c;
            int len = uart_read_bytes(uart_num_, (uint8_t*)&c, 1, pdMS_TO_TICKS(5)); //5ms timeout, non-blocking
            return (len == 1) ? c : 0;
        }

        bool byte_available() override {
            size_t buffered_size = 0;
            uart_get_buffered_data_len(uart_num_, &buffered_size);
            return buffered_size > 0;
        }

        void put_bytes(const char* data, size_t len) override {
            uart_write_bytes(uart_num_, data, len);
        }

        size_t get_bytes(char* buffer, size_t max_len) override {
            int len = uart_read_bytes(uart_num_, (uint8_t*)buffer, max_len, pdMS_TO_TICKS(5)); //5ms timeout, non-blocking
            return (len > 0) ? len : 0;
        }


    private:
        uart_port_t uart_num_;

        void init_uart(int baud_rate, gpio_num_t tx_pin, gpio_num_t rx_pin) {
            // UART configuration
            uart_config_t uart_config = {
                .baud_rate = baud_rate,
                .data_bits = UART_DATA_8_BITS,
                .parity    = UART_PARITY_DISABLE,
                .stop_bits = UART_STOP_BITS_1,
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                .rx_flow_ctrl_thresh = 122,
                .source_clk = UART_SCLK_APB,
                .flags = 0,
            };

            // Configure UART parameters
            ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
            ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
            ESP_ERROR_CHECK(uart_driver_install(uart_num_, 1024, 0, 0, nullptr, 0));
        }
};