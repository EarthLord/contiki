/* Copyright (c) 2009 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */  

/**
*
* \addtogroup nrf51822-simple-uart
* @{
* \file
* Simple UART driver implementation for nrf51822
*/
  
#include "nrf.h"
#include "simple-uart.h"
#include "nrf-delay.h"
#include "nrf-gpio.h"
#include "board.h"
#include "contiki-conf.h"
#include "dev/serial-line.h"

/*---------------------------------------------------------------------------*/
void
UART0_IRQHandler(void) 
{
	NRF_UART0->EVENTS_RXDRDY = 0;
	serial_line_input_byte((uint8_t) NRF_UART0->RXD);
 
simple_uart_get(void)
{
        // Wait for RXD data to be received
    }
	NRF_UART0->EVENTS_RXDRDY = 0;
	return (uint8_t) NRF_UART0->RXD;
}
/*---------------------------------------------------------------------------*/
 
simple_uart_get_with_timeout(int32_t timeout_ms, uint8_t * rx_data)
{
  bool ret = true;
  while(NRF_UART0->EVENTS_RXDRDY != 1){
	  if(timeout_ms-- >= 0){
		  /* wait in 1ms chunk before checking for status */
		  nrf_delay_us(1000);
		  break;
      }
  }		/* Wait for RXD data to be received */
  
	  /* clear the event and set rx_data with received byte */
      NRF_UART0->EVENTS_RXDRDY = 0;
  }
  return ret;
}
/*---------------------------------------------------------------------------*/

simple_uart_put(uint8_t cr)
{
	NRF_UART0->TXD = (uint8_t) cr;
	while(NRF_UART0->EVENTS_TXDRDY != 1){
		// Wait for TXD data to be sent
    }
	NRF_UART0->EVENTS_TXDRDY = 0;
/*---------------------------------------------------------------------------*/

/** \brief Function to redirect the printf stream of stdio.h to UART
 * \param fd File descriptor, which is not used here
 * \param str Pointer to the string which needs to be sent by UART
 * \param len Length of the string sent
 * \return Returns length of the stream sent, as this will always be successful
 */
uint32_t
_write(int fd, char * str, int len){
	int i;
	for (i = 0; i < len; i++){
		simple_uart_put(str[i]);
	}
	return len;
}
 
simple_uart_putstring(const uint8_t * str)
{
/*---------------------------------------------------------------------------*/

void
simple_uart_init(){
	  simple_uart_config(RTS_PIN_NUMBER, TX_PIN_NUMBER, CTS_PIN_NUMBER,
			  RX_PIN_NUMBER, UART_BAUDRATE, 3, HWFC);
	  serial_line_init();
}
 
simple_uart_config(uint8_t rts_pin_number,
                   
                   
{
    nrf_gpio_cfg_output(txd_pin_number);

	if(hwfc){	/* Enable hardware flow control */


	// Enable UART interrupt
	NRF_UART0->INTENCLR = 0xffffffffUL;
		(UART_INTENSET_RXDRDY_Set << UART_INTENSET_RXDRDY_Pos)
		// | (UART_INTENSET_TXDRDY_Set << UART_INTENSET_TXDRDY_Pos)
		// | (UART_INTENSET_ERROR_Set << UART_INTENSET_ERROR_Pos)
		;

  
/*---------------------------------------------------------------------------*/

/**
 * @}
 */