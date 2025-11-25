#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>

/* ==========================================
 * CONFIGURAÇÃO DA PLACA (MUDE AQUI!)
 * ==========================================
 * Defina 1 para a placa que começa recebendo (RX)
 * Defina 2 para a placa que começa enviando (TX)
 */
#define PLACA_ID  1   

/* --- Hardware --- */
#define UART_DEV_NODE DT_CHOSEN(zephyr_shell_uart)
#define SW0_NODE      DT_ALIAS(sw0)
#define LED_RX_NODE   DT_ALIAS(led0) // Verde
#define LED_TX_NODE   DT_ALIAS(led1) // Vermelho

/* --- Constantes --- */
#define STACK_SIZE    1024
#define PRIORITY      7
#define MSG_SIZE      64
#define TEMPO_FASE_MS 5000 // 5 segundos

/* --- Eventos --- */
#define EVT_RESET_SYNC  BIT(0) // Evento do botão externo

/* --- Objetos do Kernel --- */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);
K_EVENT_DEFINE(sinc_events);

/* --- Hardware structs --- */
const struct device *uart_dev = DEVICE_DT_GET(UART_DEV_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios, {0});
static const struct gpio_dt_spec led_rx = GPIO_DT_SPEC_GET(LED_RX_NODE, gpios);
static const struct gpio_dt_spec led_tx = GPIO_DT_SPEC_GET(LED_TX_NODE, gpios);
static struct gpio_callback button_cb_data;

/* Estado Global */
enum Estado { ESTADO_TX, ESTADO_RX };
volatile enum Estado estado_atual;

/* --- Funções de UART --- */
void uart_send_string(const char *str) {
    for (int i = 0; i < strlen(str); i++) {
        uart_poll_out(uart_dev, str[i]);
    }
}

/* Callback de RX (Apenas armazena dados, não controla mais fluxo) */
void serial_cb(const struct device *dev, void *user_data) {
    uint8_t c;
    static char rx_buf[MSG_SIZE];
    static int rx_pos = 0;

    if (!uart_irq_update(dev)) return;

    if (uart_irq_rx_ready(dev)) {
        while (uart_fifo_read(dev, &c, 1) == 1) {
            // Bufferiza até encontrar \n ou encher
            if ((c == '\n' || c == '\r') && rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
                rx_pos = 0;
            } else if (rx_pos < (MSG_SIZE - 1)) {
                rx_buf[rx_pos++] = c;
            }
        }
    }
}

/* Callback do Botão (Trigger Externo) */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    // Avisa a thread gerente para resetar o ciclo IMEDIATAMENTE
    k_event_post(&sinc_events, EVT_RESET_SYNC);
}

/* --- Threads --- */

/* Thread 1: Processamento de Dados (Imprime o que recebe) */
void comm_thread_entry(void) {
    char msg[MSG_SIZE];
    while (1) {
        if (k_msgq_get(&uart_msgq, &msg, K_FOREVER) == 0) {
            // Só imprime se estivermos no estado de ouvir
            if (estado_atual == ESTADO_RX) {
                printk("[RX RECV]: %s\n", msg);
            }
        }
    }
}

/* Thread 2: Gerente de Estado e Tempo (Coração do sistema) */
void manager_thread_entry(void) {
    // 1. Configuração Inicial
    if (!device_is_ready(uart_dev) || !device_is_ready(led_rx.port) || !device_is_ready(button.port)) return;

    gpio_pin_configure_dt(&led_rx, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_tx, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    // Define estado inicial baseado no ID da placa
    #if PLACA_ID == 1
        estado_atual = ESTADO_RX; // Placa 1 começa ouvindo
        printk("--- PLACA 1 INICIADA (RX PADRAO) ---\n");
    #else
        estado_atual = ESTADO_TX; // Placa 2 começa falando
        printk("--- PLACA 2 INICIADA (TX PADRAO) ---\n");
    #endif

    while (1) {
        // A) Atualiza LEDs e Hardware conforme estado
        if (estado_atual == ESTADO_TX) {
            gpio_pin_set_dt(&led_tx, 1);
            gpio_pin_set_dt(&led_rx, 0);
            printk(">>> Fase TX (Enviando dados...)\n");
            
            // Simula envio de dados
            uart_send_string("Dados da Placa " STRINGIFY(PLACA_ID) "\r\n");
        } else {
            gpio_pin_set_dt(&led_tx, 0);
            gpio_pin_set_dt(&led_rx, 1);
            printk("<<< Fase RX (Aguardando...)\n");
        }

        // B) Espera Inteligente (Sleep ou Evento)
        // A thread dorme aqui por 5s. SE o botão for apertado, ela acorda NA HORA.
        uint32_t eventos = k_event_wait(&sinc_events, EVT_RESET_SYNC, true, K_MSEC(TEMPO_FASE_MS));

        if (eventos & EVT_RESET_SYNC) {
            // C) Sincronismo Externo Detectado!
            printk("!!! HARD SYNC DETECTADO !!! Reiniciando ciclo.\n");
            
            // Força o estado correto para cada placa
            #if PLACA_ID == 1
                estado_atual = ESTADO_RX; // Placa 1 sempre reseta para RX
            #else
                estado_atual = ESTADO_TX; // Placa 2 sempre reseta para TX
            #endif
            
            // Não invertemos o estado aqui, o loop vai rodar e aplicar o estado definido acima
        } else {
            // D) Timeout (Passaram 5s naturalmente)
            // Inverte o estado
            estado_atual = (estado_atual == ESTADO_TX) ? ESTADO_RX : ESTADO_TX;
        }
    }
}

/* Definição das Threads */
K_THREAD_DEFINE(manager_tid, STACK_SIZE, manager_thread_entry, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(comm_tid, STACK_SIZE, comm_thread_entry, NULL, NULL, NULL, PRIORITY, 0, 0);

void main(void) {
    k_sleep(K_FOREVER);
}