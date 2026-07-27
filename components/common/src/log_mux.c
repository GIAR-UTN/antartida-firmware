/**
 * @file log_mux.c
 * @brief Custom LOG function definition.
 */
#include "log_mux.h"

static int custom_vprintf(const char *fmt, va_list ap);

static vprintf_like_t s_orig_vprintf = NULL;
static const char *TAG = "log_mux";


esp_err_t log_mux_init(void)
{
    s_orig_vprintf = esp_log_set_vprintf(custom_vprintf);
    ESP_LOGI(TAG, "LOG Mutiplexer was initialized successfully.");
    return ESP_OK;
}


static int calculate_size_print(const char *fmt, va_list ap)
{
    // Returns 0 on error, otherwise returns size of buff
    int r = 0;

    va_list ap1;
    va_copy(ap1, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap1);
    va_end(ap1);
    if (needed <= 0) return r;

    return needed;
}

static int custom_vprintf(const char *fmt, va_list ap)
{
    int r = 0;
    int needed = 0;
    char stack_buf[256];
    char *buf = stack_buf;
    
    // NOTE: We discard the original vprintf function..
    // if (s_orig_vprintf) {
    //     va_list ap0;
    //     va_copy(ap0, ap);
    //     r = s_orig_vprintf(fmt, ap0);
    //     va_end(ap0);
    // }

    needed = calculate_size_print(fmt, ap);
    
    // Create another buf (malloc) if size if greter than 256 chars.
    if (needed + 1 > (int)sizeof(stack_buf)) {
        buf = (char *)malloc(needed + 1);
        if (!buf) return r;
    }

    // Copy contents to buffer
    va_list ap1;
    va_copy(ap1, ap);
    vsnprintf(buf, needed + 1, fmt, ap1);
    va_end(ap1);

    // Broadcast to services
    uart_write_bytes(UART_NUM_2, buf, needed);
    uart_write_bytes(UART_NUM_2, "\r", 2);

    uart_write_bytes(UART_NUM_0, buf, needed);
    uart_write_bytes(UART_NUM_0, "\r", 2);

    if (buf != stack_buf) free(buf);
    return r;
}



