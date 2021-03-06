/*
 * Pillar-func.c
 *
 *  Created on: 29-Apr-2014
 *      Author: Prithvi
 */
/* Copyright (c) 2013 Nordic Semiconductor. All Rights Reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the license.txt file.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Pillar-func.h"
#include "clock-nrf.h"
#include "leds.h"
#include "nordic_common.h"
#include "nrf.h"
#include "app_error.h"
#include "nrf51_bitfields.h"
#include "ble.h"
#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "board.h"
#include "app_scheduler.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "ble_error_log.h"
#include "ble_debug_assert_handler.h"
#include "pstorage.h"
#include "ble_lbs.h"
#include "contiki.h"
#include "serial-line.h"
#include "ble_l2cap.h"

PROCESS(alarm_timer_process, "Alarm timer process");
PROCESS(alarm_ring_process, "Alarm ring process");
PROCESS(l2cap_process, "L2CAP process");

#define ADVERTISING_LED_PIN_NO          LEDS_RED								/**< Is on when device is advertising. */
#define CONNECTED_LED_PIN_NO            LEDS_GREEN                              /**< Is on when device has connected. */
#define LEDBUTTON_LED_PIN_NO            LEDS_BLUE

#define RX_BUFFER_SIZE					8
#define TX_BUFFER_SIZE					8

#define DEVICE_NAME                     "Pillar"                           			/**< Name of device. Will be included in the advertising data. */

#define APP_ADV_INTERVAL                2000                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */
#define APP_ADV_TIMEOUT_IN_SECONDS      0x0000										/**< The advertising timeout (in units of seconds). */

#define APP_TIMER_PRESCALER             COUNTER_PRESCALER		                    /**< Value of the RTC1 PRESCALER register. */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(100, UNIT_1_25_MS)            /**< Minimum acceptable connection interval (0.5 seconds). */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(400, UNIT_1_25_MS)           /**< Maximum acceptable connection interval (1 second). */
#define SLAVE_LATENCY                   1                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds). */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(20000, APP_TIMER_PRESCALER) /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (15 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time between each call to sd_ble_gap_conn_param_update after the first call (5 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

#define APP_GPIOTE_MAX_USERS            1                                           /**< Maximum number of users of the GPIOTE handler. */

#define BUTTON_DETECTION_DELAY          APP_TIMER_TICKS(50, APP_TIMER_PRESCALER)    /**< Delay from a GPIOTE event until a button is reported as pushed (in number of timer ticks). */

#define SEC_PARAM_TIMEOUT               30                                          /**< Timeout for Pairing Request or Security Request (in seconds). */
#define SEC_PARAM_BOND                  1                                           /**< Perform bonding. */
#define SEC_PARAM_MITM                  0                                           /**< Man In The Middle protection not required. */
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                        /**< No I/O capabilities. */
#define SEC_PARAM_OOB                   0                                           /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE          7                                           /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE          16                                          /**< Maximum encryption key size. */

#define DEAD_BEEF                       0xDEADBEEF                                  /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

typedef enum {
	ALARM_NOT_SET,
	ALARM_SET_NOT_EXPIRED,
	ALARM_EXPIRED
}alarm_states;

//static app_timer_id_t 					alarm_state_timer;
//static app_timer_id_t 					led_blink_timer;
static ble_gap_sec_params_t             m_sec_params;                               /**< Security requirements for this application. */
static uint16_t                         m_conn_handle = BLE_CONN_HANDLE_INVALID;    /**< Handle of the current connection. */
static ble_lbs_t                        m_lbs;
static alarm_states						current_state = ALARM_NOT_SET;

/**@brief Function for error handling, which is called when an error has occurred.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of error.
 *
 * @param[in] error_code  Error code supplied to the handler.
 * @param[in] line_num    Line number where the handler is called.
 * @param[in] p_file_name Pointer to the file name.
 */
void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
	// This call can be used for debug purposes during application development.
	// @note CAUTION: Activating this code will write the stack to flash on an error.
	//                This function should NOT be used in a final product.
	//                It is intended STRICTLY for development/debugging purposes.
	//                The flash write will happen EVEN if the radio is active, thus interrupting
	//                any communication.
	//                Use with care. Un-comment the line below to use.
	//ble_debug_assert_handler(error_code, line_num, p_file_name);

	printf("Error of 0x%X at %d in  %s", error_code, line_num, p_file_name);

	for(;;);

	// On assert, the system can only recover with a reset.
	//NVIC_SystemReset();
}


/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in]   line_num   Line number of the failing ASSERT call.
 * @param[in]   file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
	app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

static void uart_event_handler(void)
{
	current_state = ALARM_NOT_SET;

	uint32_t err_code = ble_lbs_on_button_change(&m_lbs, 0x31);
	if (err_code != NRF_SUCCESS &&
			err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
			err_code != NRF_ERROR_INVALID_STATE)
	{
		APP_ERROR_CHECK(err_code);
	}
}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
void gap_params_init(void)
{
	uint32_t                err_code;
	ble_gap_conn_params_t   gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(&sec_mode,
			(const uint8_t *)DEVICE_NAME,
			strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency     = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Advertising functionality.
 *
 * @details Encodes the required advertising data and passes it to the stack.
 *          Also builds a structure to be passed to the stack when starting advertising.
 */
void advertising_init(void)
{
	uint32_t      err_code;
	ble_advdata_t advdata;
	ble_advdata_t scanrsp;
	uint8_t       flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

	ble_uuid_t adv_uuids[] = {{LBS_UUID_SERVICE, m_lbs.uuid_type}};

	// Build and set advertising data
	memset(&advdata, 0, sizeof(advdata));

	advdata.name_type               = BLE_ADVDATA_FULL_NAME;
	advdata.include_appearance      = true;
	advdata.flags.size              = sizeof(flags);
	advdata.flags.p_data            = &flags;

	memset(&scanrsp, 0, sizeof(scanrsp));
	scanrsp.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(adv_uuids[0]);
	scanrsp.uuids_complete.p_uuids  = adv_uuids;

	err_code = ble_advdata_set(&advdata, &scanrsp);
	APP_ERROR_CHECK(err_code);
}

static void alarm_state_timer_handler(void * p_context){

	process_start(&alarm_ring_process, NULL);
	current_state = ALARM_EXPIRED;

	uint32_t err_code = ble_lbs_on_alarm(&m_lbs, 0x00000000);
	if (err_code != NRF_SUCCESS &&
			err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
			err_code != NRF_ERROR_INVALID_STATE)
	{
		APP_ERROR_CHECK(err_code);
	}
}

static void alarm_write_handler(ble_lbs_t *p_lbs, uint32_t alarm_time){
	if(alarm_time != 0){
		if(current_state != ALARM_EXPIRED){
			current_state = ALARM_SET_NOT_EXPIRED;
			if(process_is_running(&alarm_timer_process)){
				process_exit(&alarm_timer_process);
				process_start(&alarm_timer_process, (const char *) &alarm_time);
			}else{
				process_start(&alarm_timer_process, (const char *) &alarm_time);
			}
			uint32_t err_code = ble_lbs_on_button_change(&m_lbs, 0x30);
			if (err_code != NRF_SUCCESS &&
					err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
					err_code != NRF_ERROR_INVALID_STATE)
			{
				APP_ERROR_CHECK(err_code);
			}
		}
		else{
			uint32_t err_code = ble_lbs_on_alarm(&m_lbs, 0x00000000);
			if (err_code != NRF_SUCCESS &&
					err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
					err_code != NRF_ERROR_INVALID_STATE)
			{
				APP_ERROR_CHECK(err_code);
			}
		}
	}
	else{
		if(current_state == ALARM_SET_NOT_EXPIRED){
			current_state = ALARM_NOT_SET;
			process_exit(&alarm_timer_process);
			uint32_t err_code = ble_lbs_on_alarm(&m_lbs, 0x00000000);
			if (err_code != NRF_SUCCESS &&
					err_code != BLE_ERROR_INVALID_CONN_HANDLE &&
					err_code != NRF_ERROR_INVALID_STATE)
			{
				APP_ERROR_CHECK(err_code);
			}
		}
	}

	printf("AlarmTime:%d\n",alarm_time);
}

/**@brief Function for initializing services that will be used by the application.
 */
void services_init(void)
{
	uint32_t err_code;
	ble_lbs_init_t init;

	init.alarm_write_handler = alarm_write_handler;

	err_code = ble_lbs_init(&m_lbs, &init);
	APP_ERROR_CHECK(err_code);

	uint16_t len[2] = {0x0001,0x0000};
	uint8_t zero = '1';
	const uint8_t * const reset_value = &zero;
	err_code = sd_ble_gatts_value_set(m_lbs.button_char_handles.value_handle, 0, len, reset_value);
	APP_ERROR_CHECK(err_code);

}


/**@brief Function for initializing security parameters.
 */
void sec_params_init(void)
{
	m_sec_params.timeout      = SEC_PARAM_TIMEOUT;
	m_sec_params.bond         = SEC_PARAM_BOND;
	m_sec_params.mitm         = SEC_PARAM_MITM;
	m_sec_params.io_caps      = SEC_PARAM_IO_CAPABILITIES;
	m_sec_params.oob          = SEC_PARAM_OOB;
	m_sec_params.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
	m_sec_params.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
}


/**@brief Function for handling the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module which
 *          are passed to the application.
 *          @note All this function does is to disconnect. This could have been done by simply
 *                setting the disconnect_on_fail config parameter, but instead we use the event
 *                handler mechanism to demonstrate its use.
 *
 * @param[in]   p_evt   Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
	uint32_t err_code;

	if(p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
	{
		err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
		APP_ERROR_CHECK(err_code);
	}
}


/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}


/**@brief Function for initializing the Connection Parameters module.
 */
void conn_params_init(void)
{
	uint32_t               err_code;
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params                  = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail             = false;
	cp_init.evt_handler                    = on_conn_params_evt;
	cp_init.error_handler                  = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 */
void advertising_start(void)
{
	uint32_t             err_code;
	ble_gap_adv_params_t adv_params;

	// Start advertising
	memset(&adv_params, 0, sizeof(adv_params));

	adv_params.type        = BLE_GAP_ADV_TYPE_ADV_IND;
	adv_params.p_peer_addr = NULL;
	adv_params.fp          = BLE_GAP_ADV_FP_ANY;
	adv_params.interval    = APP_ADV_INTERVAL;
	adv_params.timeout     = APP_ADV_TIMEOUT_IN_SECONDS;

	err_code = sd_ble_gap_adv_start(&adv_params);
	APP_ERROR_CHECK(err_code);
	leds_on(ADVERTISING_LED_PIN_NO);
}


/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
	uint32_t                         err_code;
	static ble_gap_evt_auth_status_t m_auth_status;
	ble_gap_enc_info_t *             p_enc_info;

	switch (p_ble_evt->header.evt_id)
	{
	case BLE_GAP_EVT_CONNECTED:
		leds_on(CONNECTED_LED_PIN_NO);
		leds_off(ADVERTISING_LED_PIN_NO);
		m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;

		//err_code = app_button_enable();
		//APP_ERROR_CHECK(err_code);
		break;

	case BLE_GAP_EVT_DISCONNECTED:
		leds_off(CONNECTED_LED_PIN_NO);
		m_conn_handle = BLE_CONN_HANDLE_INVALID;

		current_state = ALARM_NOT_SET;
		leds_off(LEDBUTTON_LED_PIN_NO);

		//err_code = app_button_disable();
		//APP_ERROR_CHECK(err_code);

		advertising_start();
		break;

	case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
		err_code = sd_ble_gap_sec_params_reply(m_conn_handle,
				BLE_GAP_SEC_STATUS_SUCCESS,
				&m_sec_params);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_GATTS_EVT_SYS_ATTR_MISSING:
		err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_GAP_EVT_AUTH_STATUS:
		m_auth_status = p_ble_evt->evt.gap_evt.params.auth_status;
		break;

	case BLE_GAP_EVT_SEC_INFO_REQUEST:
		p_enc_info = &m_auth_status.periph_keys.enc_info;
		if (p_enc_info->div == p_ble_evt->evt.gap_evt.params.sec_info_request.div)
		{   /**< Array of timer nodes. */
			err_code = sd_ble_gap_sec_info_reply(m_conn_handle, p_enc_info, NULL);
		APP_ERROR_CHECK(err_code);
		}
		else
		{
			// No keys found for this device
			err_code = sd_ble_gap_sec_info_reply(m_conn_handle, NULL, NULL);
			APP_ERROR_CHECK(err_code);
		}
		break;

	case BLE_GAP_EVT_TIMEOUT:
		if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISEMENT)
		{
			leds_off(ADVERTISING_LED_PIN_NO);

			printf("shutDown");
			err_code = sd_power_system_off();
			APP_ERROR_CHECK(err_code);

		}
		break;

	default:
		// No implementation needed.
		break;
	}
}


/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 *          event has been received.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
	on_ble_evt(p_ble_evt);
	ble_conn_params_on_ble_evt(p_ble_evt);
	ble_lbs_on_ble_evt(&m_lbs, p_ble_evt);
}


/**@brief Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in]   sys_evt   System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt)
{
	pstorage_sys_event_handler(sys_evt);
}


/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
void ble_stack_init(void)
{
	uint32_t err_code;

	// Initialize the SoftDevice handler module.
	SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, false);
	// Register with the SoftDevice handler module for BLE events.
	err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
	APP_ERROR_CHECK(err_code);
	// Register with the SoftDevice handler module for BLE events.
	err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
	APP_ERROR_CHECK(err_code);
}

void l2cap_init(void){
	process_start(&l2cap_process, NULL);
}

PROCESS_THREAD(alarm_timer_process, ev, data)
{
  PROCESS_BEGIN();

  uint32_t timer_ticks = *((uint32_t *) data);
  timer_ticks = timer_ticks * CLOCK_CONF_SECOND;

  static struct etimer et_alarm_timer;
  etimer_set(&et_alarm_timer, timer_ticks);

  while((ev != PROCESS_EVENT_TIMER) && (ev != PROCESS_EVENT_EXIT)){
	  PROCESS_YIELD();
  }
  if(ev == PROCESS_EVENT_TIMER){
	  printf("Ring!\n");
	  alarm_state_timer_handler(NULL);
  }
  if(ev == PROCESS_EVENT_EXIT){
	  etimer_stop(&et_alarm_timer);
	  printf("NoAlarm\n");
  }
  PROCESS_END();
}

PROCESS_THREAD(alarm_ring_process, ev, data)
{
  PROCESS_BEGIN();

  static struct etimer et_alarm_ring;
  etimer_set(&et_alarm_ring, CLOCK_CONF_SECOND);

  while(1){
	  PROCESS_YIELD();
	  if(ev == PROCESS_EVENT_TIMER){
		  leds_toggle(LEDBUTTON_LED_PIN_NO);
		  etimer_reset(&et_alarm_ring);
	  }
	  if(ev == serial_line_event_message){
		  uint8_t * recd = (uint8_t *) data;
		  if(recd[0] == 'O' && recd[1] == 'n' && recd[2] == 'e'){
			  etimer_stop(&et_alarm_ring);
			  printf("Stopped\n");
			  leds_on(LEDBUTTON_LED_PIN_NO);
			  uart_event_handler();
			  process_exit(&alarm_ring_process);
		  }
	  }
  }

  PROCESS_END();
}


PROCESS_THREAD(l2cap_process, ev, data)
{
  PROCESS_BEGIN();
  sd_ble_l2cap_cid_register(0x41);
  while(1){
		PROCESS_YIELD();
		if(ev == serial_line_event_message){
			uint8_t * recd = (uint8_t *) data;
			if(recd[0] == 'T' && recd[1] == 'w' && recd[2] == 'o'){
				uint32_t err_code;
				ble_l2cap_header_t tx_head;

				uint8_t tx_data[] = "Howdy!";
				printf("%s\n",tx_data);

				tx_head.cid = 0x41;
				tx_head.len = sizeof(tx_data);

				err_code = sd_ble_l2cap_tx (m_conn_handle, &tx_head, tx_data);
				if (err_code != BLE_ERROR_NO_TX_BUFFERS) {
						APP_ERROR_CHECK(err_code);
				}else{
					printf("No buffers available\n");
				}
			}
		}
  }
  PROCESS_END();
}
