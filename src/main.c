#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

/* * Obtém os dispositivos definidos no Device Tree.
 * 'lpsci0' é a UART0 (Console/USB).
 * 'uart1' é a UART conectada à outra placa (PTE0/PTE1).
 */
const struct device *uart_pc = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
const struct device *uart_peer = DEVICE_DT_GET(DT_NODELABEL(uart1));

int main(void)
{
    unsigned char recv_char;

    // 1. Verificação de Segurança: Os dispositivos estão prontos?
    if (!device_is_ready(uart_pc)) {
        printk("Erro: UART PC (lpsci0) não está pronta.\n");
        return 0;
    }
    if (!device_is_ready(uart_peer)) {
        printk("Erro: UART Peer (uart1) não está pronta.\n");
        return 0;
    }

    printk("\n--- Chat Iniciado Zephyr RTOS (Polling) ---\n");
    printk("--- Use UART0 (USB) e UART1 (PTE0/PTE1) ---\n");

    // 2. Loop Infinito (Polling)
    while (1) {
        
        // --- CHECAGEM 1: Chegou algo do PC? ---
        // uart_poll_in retorna 0 se leu com sucesso, -1 se o buffer está vazio.
        if (uart_poll_in(uart_pc, &recv_char) == 0) {
            
            // Opcional: Eco local para ver o que digita
            // uart_poll_out(uart_pc, recv_char); 

            // Envia para a outra placa via UART1
            uart_poll_out(uart_peer, recv_char);
        }

        // --- CHECAGEM 2: Chegou algo da outra placa? ---
        if (uart_poll_in(uart_peer, &recv_char) == 0) {
            
            // Recebeu da UART1, envia para o PC para mostrar na tela
            uart_poll_out(uart_pc, recv_char);
        }

        /* * Dica de RTOS: Em um sistema real, é educado colocar um 
         * k_sleep(K_MSEC(1)) ou k_yield() aqui para não ocupar 100% da CPU
         * checando vazio, o que economiza bateria. Mas para este teste,
         * o loop puro funciona perfeitamente.
         */
        k_yield(); 
    }
    return 0;
}