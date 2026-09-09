#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

#define UART1_RX_RING_SIZE   1536U
#define UART1_LINE_Q_DEPTH   2U

static volatile uint8_t  s_rx_ring[UART1_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint32_t s_rx_byte_count = 0U;
static volatile uint32_t s_rx_drop_count = 0U;

static char     s_asm_buf[BSP_UART1_RX_LINE_MAX];
static uint16_t s_asm_len = 0U;

static char     s_line_q[UART1_LINE_Q_DEPTH][BSP_UART1_RX_LINE_MAX];
static uint8_t  s_line_q_head = 0U;
static uint8_t  s_line_q_tail = 0U;
static uint8_t  s_line_q_count = 0U;
static uint8_t  s_ota_binary = 0U;

static void uart1_rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) % UART1_RX_RING_SIZE);

    s_rx_byte_count++;
    if(next != s_rx_tail) {
        s_rx_ring[s_rx_head] = b;
        s_rx_head = next;
    } else {
        s_rx_drop_count++;
    }
}

static uint8_t uart1_line_enqueue(const char *line)
{
    if(line == NULL) {
        return 0U;
    }
    if(s_line_q_count >= UART1_LINE_Q_DEPTH) {
        s_line_q_head = (uint8_t)((s_line_q_head + 1U) % UART1_LINE_Q_DEPTH);
        s_line_q_count--;
        s_rx_drop_count++;
    }
    strncpy(s_line_q[s_line_q_tail], line, BSP_UART1_RX_LINE_MAX - 1U);
    s_line_q[s_line_q_tail][BSP_UART1_RX_LINE_MAX - 1U] = '\0';
    s_line_q_tail = (uint8_t)((s_line_q_tail + 1U) % UART1_LINE_Q_DEPTH);
    s_line_q_count++;
    return 1U;
}

void BSP_UART1_DrainHwFifo(void)
{
    /*
     * RX IRQ and foreground polling must not read RBR concurrently.  Mask
     * only UART1 while moving any pending FIFO bytes into the shared ring.
     */
    PFIC_DisableIRQ(UART1_IRQn);
    while(R8_UART1_RFC) {
        uart1_rx_push(R8_UART1_RBR);
    }
    PFIC_EnableIRQ(UART1_IRQn);
}

uint32_t BSP_UART1_GetRxByteCount(void)
{
    return s_rx_byte_count;
}

uint32_t BSP_UART1_GetRxDropCount(void)
{
    return s_rx_drop_count;
}

static uint8_t uart1_rx_pop(uint8_t *out)
{
    if(s_rx_head == s_rx_tail) {
        return 0U;
    }
    *out = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % UART1_RX_RING_SIZE);
    return 1U;
}

#include "CH58x_gpio.h"

void BSP_UART1_Init(uint32_t baudrate)
{
    GPIOPinRemap(DISABLE, RB_PIN_UART1);
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);

    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_byte_count = 0U;
    s_rx_drop_count = 0U;
    s_asm_len = 0U;
    s_line_q_head = 0U;
    s_line_q_tail = 0U;
    s_line_q_count = 0U;

    UART1_DefInit();
    UART1_BaudRateCfg(baudrate);
    UART1_ByteTrigCfg(UART_1BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);
}

void BSP_UART1_SendString(const char *str)
{
    if(str == NULL) {
        return;
    }
    UART1_SendString((uint8_t *)str, (uint16_t)strlen(str));
}

void BSP_UART1_SendBytes(const uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0U) {
        return;
    }
    UART1_SendString((uint8_t *)data, len);
}

void BSP_UART1_SetOtaBinaryMode(uint8_t enable)
{
    s_ota_binary = enable ? 1U : 0U;
    s_asm_len = 0U;
    s_line_q_head = 0U;
    s_line_q_tail = 0U;
    s_line_q_count = 0U;
}

uint8_t BSP_UART1_IsOtaBinaryMode(void)
{
    return s_ota_binary;
}

uint8_t BSP_UART1_PollByte(uint8_t *out)
{
    if(out == NULL) {
        return 0U;
    }
    return uart1_rx_pop(out);
}

void BSP_UART1_SendHeartJSON(uint8_t hr, uint8_t spo2, float pwv)
{
    char buf[96];
    int  n = snprintf(buf, sizeof(buf),
                      "{\"hr\":%d,\"spo2\":%d,\"pwv\":%lu.%01lu}\r\n",
                      hr, spo2,
                      (unsigned long)((uint32_t)(pwv * 10.0f + 0.5f) / 10U),
                      (unsigned long)((uint32_t)(pwv * 10.0f + 0.5f) % 10U));
    if(n > 0) {
        UART1_SendString((uint8_t *)buf, (uint16_t)n);
    }
}

static void uart1_assemble_byte(uint8_t b)
{
    if(b == '\r') {
        return;
    }
    if(b == '\n') {
        if(s_asm_len > 0U) {
            s_asm_buf[s_asm_len] = '\0';
            (void)uart1_line_enqueue(s_asm_buf);
            s_asm_len = 0U;
        }
        return;
    }
    if(s_asm_len + 1U >= BSP_UART1_RX_LINE_MAX) {
        s_asm_len = 0U;
        return;
    }
    s_asm_buf[s_asm_len++] = (char)b;
}

const char *BSP_UART1_PollLinePtr(void)
{
    uint8_t b;
    const char *line;

    if(s_ota_binary) {
        return NULL;
    }

    BSP_UART1_DrainHwFifo();

    while(uart1_rx_pop(&b)) {
        uart1_assemble_byte(b);
    }

    if(s_line_q_count == 0U) {
        return NULL;
    }

    line = s_line_q[s_line_q_head];
    s_line_q_head = (uint8_t)((s_line_q_head + 1U) % UART1_LINE_Q_DEPTH);
    s_line_q_count--;
    return line;
}

uint8_t BSP_UART1_PollLine(char *buf, uint16_t buf_len)
{
    const char *line = BSP_UART1_PollLinePtr();

    if(line == NULL || buf == NULL || buf_len == 0U) {
        return 0U;
    }
    strncpy(buf, line, (size_t)buf_len - 1U);
    buf[buf_len - 1U] = '\0';
    return 1U;
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
    switch(UART1_GetITFlag()) {
    case UART_II_LINE_STAT:
        UART1_GetLinSTA();
        break;

    case UART_II_RECV_RDY:
    case UART_II_RECV_TOUT:
        while(R8_UART1_RFC) {
            uart1_rx_push(UART1_RecvByte());
        }
        break;

    case UART_II_THR_EMPTY:
        break;

    default:
        break;
    }
}
