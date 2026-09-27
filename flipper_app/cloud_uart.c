#include "cloud_uart.h"

#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#define RX_BUFFER_SIZE  4096
#define LINE_MAX_LENGTH 512

struct CloudUart {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
};

static void
    cloud_uart_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    CloudUart* uart = context;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t byte = furi_hal_serial_async_rx(handle);
            furi_stream_buffer_send(uart->rx_stream, &byte, 1, 0);
        }
    }
}

CloudUart* cloud_uart_alloc(uint32_t baudrate) {
    FuriHalSerialHandle* serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!serial) return NULL;

    CloudUart* uart = malloc(sizeof(CloudUart));
    uart->serial = serial;
    uart->rx_stream = furi_stream_buffer_alloc(RX_BUFFER_SIZE, 1);

    furi_hal_serial_init(uart->serial, baudrate);
    furi_hal_serial_async_rx_start(uart->serial, cloud_uart_rx_callback, uart, false);
    return uart;
}

void cloud_uart_free(CloudUart* uart) {
    furi_assert(uart);
    furi_hal_serial_async_rx_stop(uart->serial);
    furi_hal_serial_deinit(uart->serial);
    furi_hal_serial_control_release(uart->serial);
    furi_stream_buffer_free(uart->rx_stream);
    free(uart);
}

void cloud_uart_flush_rx(CloudUart* uart) {
    uint8_t buffer[64];
    while(furi_stream_buffer_receive(uart->rx_stream, buffer, sizeof(buffer), 0) > 0) {
    }
}

void cloud_uart_send(CloudUart* uart, const uint8_t* data, size_t size) {
    furi_hal_serial_tx(uart->serial, data, size);
    furi_hal_serial_tx_wait_complete(uart->serial);
}

void cloud_uart_send_line(CloudUart* uart, const char* line) {
    cloud_uart_send(uart, (const uint8_t*)line, strlen(line));
    cloud_uart_send(uart, (const uint8_t*)"\n", 1);
}

static uint32_t cloud_uart_remaining(uint32_t start, uint32_t timeout_ms) {
    uint32_t elapsed = furi_get_tick() - start;
    uint32_t timeout = furi_ms_to_ticks(timeout_ms);
    return elapsed >= timeout ? 0 : timeout - elapsed;
}

bool cloud_uart_read_line(CloudUart* uart, FuriString* line, uint32_t timeout_ms) {
    uint32_t start = furi_get_tick();

    while(true) {
        uint32_t remaining = cloud_uart_remaining(start, timeout_ms);
        if(remaining == 0) return false;

        uint8_t byte;
        if(furi_stream_buffer_receive(uart->rx_stream, &byte, 1, remaining) == 0) return false;

        if(byte == '\n') return true;
        if(byte == '\r' || byte == 0) continue;
        if(furi_string_size(line) < LINE_MAX_LENGTH) furi_string_push_back(line, (char)byte);
    }
}

bool cloud_uart_read_exact(CloudUart* uart, uint8_t* data, size_t size, uint32_t timeout_ms) {
    uint32_t start = furi_get_tick();
    size_t received = 0;

    while(received < size) {
        uint32_t remaining = cloud_uart_remaining(start, timeout_ms);
        if(remaining == 0) return false;
        received += furi_stream_buffer_receive(
            uart->rx_stream, data + received, size - received, remaining);
    }
    return true;
}
