//**************************************************************************
// syscalls.c 
//--------------------------------------------------------------------------

// Hiruna Vishwamith
// UOM
// 22/03/2025
//
//


//--------------------------------------------------------------------------
// gards and includes

#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include <stdint.h>
#include <stdarg.h>
// #include <stdio.h>

// UART base address.
// Canonical TX address that works in BOTH back-ends:
//   - Golden emulator (hart.h): prints a char on a store to FIFO_ADDR_TX = 0x40600004.
//   - RTL sim (testbench/uart.scala): emits putChar on any write whose low address
//     byte is 0x04 (0x40600004 qualifies and routes to the peripheral UART).
// The original Zynq PS-UART1 0xe0001030 worked in neither (emulator ignores it;
// RTL needs low byte 0x04).
#define UART_TX 0x40600004
#define UART_RX 0x40600000

// Helper function to send a character to UART
static void uart_send_char(char c) {
  volatile uint32_t *uart_tx_reg = (volatile uint32_t *)(UART_TX);
  // volatile uint32_t *uart_rx_reg = (volatile uint32_t *)(UART_RX);

  //   while((*uart_rx_reg)&16)
	// 	;

  *uart_tx_reg = (uint32_t)c; // Assuming writing to this address sends the char
}

// Helper function to send a null-terminated string to UART
void uart_send_string(const char *s) {
  while (*s != '\0') {
    uart_send_char(*s++);
  }
}

void uart_send_integer(int n) {
    char buffer[12]; // Enough for largest 32-bit integer + null terminator
    int i = 0;
    if (n == 0) {
        uart_send_char('0');
        return;
    }
    if (n < 0) {
        uart_send_char('-');
        n = -n;
    }
    while (n != 0) {
        buffer[i++] = (n % 10) + '0';
        n = n / 10;
    }
    while (i > 0) {
        uart_send_char(buffer[--i]);
    }
}

// Syscall function for baremettal printf function suporting %d and %s
void printf(const char *format, ...){
  va_list args;
  va_start(args, format);
  
  while (*format){
    if(*format == '%'){
      format++;
      if(*format == 'd'){
        int num = va_arg(args,int);
        uart_send_integer(num);
      }else if (*format == 's') {
        char *str = va_arg(args, char *);
        uart_send_string(str);
      }else{
        uart_send_char(*format);
      }
    } else{
      uart_send_char(*format);
    }

    format++;
  }

  va_end(args);
}



void __attribute__((weak)) thread_entry(int cid, int nc)
{
  // multi-threaded programs override this function.
  // for the case of single-threaded programs, only let core 0 proceed.
  while (cid != 0);
}

int __attribute__((weak)) main(int argc, char** argv)
{
  // single-threaded programs override this function.
  uart_send_string("Implement main(), foo!\n");
  return -1;
}


// void exit(int code)
// {
//   while (1);
// }


#endif // __SYSCALL_H__
