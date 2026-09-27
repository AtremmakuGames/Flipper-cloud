#pragma once

#include <furi.h>

/** Link to the ESP32 WiFi module over the GPIO USART (pins 13 TX / 14 RX). */
typedef struct CloudUart CloudUart;

/** Acquires the USART. Returns NULL when the port is used by someone else. */
CloudUart* cloud_uart_alloc(uint32_t baudrate);

void cloud_uart_free(CloudUart* uart);

/** Drops everything received so far. */
void cloud_uart_flush_rx(CloudUart* uart);

void cloud_uart_send(CloudUart* uart, const uint8_t* data, size_t size);

/** Sends `line` followed by '\n'. */
void cloud_uart_send_line(CloudUart* uart, const char* line);

/**
 * Appends received characters to `line` until '\n' ('\r' is dropped).
 * Returns true once a full line is in `line`, false on timeout (the partial
 * line is kept so the call can be repeated).
 */
bool cloud_uart_read_line(CloudUart* uart, FuriString* line, uint32_t timeout_ms);

/** Reads exactly `size` raw bytes. Returns false on timeout. */
bool cloud_uart_read_exact(CloudUart* uart, uint8_t* data, size_t size, uint32_t timeout_ms);
