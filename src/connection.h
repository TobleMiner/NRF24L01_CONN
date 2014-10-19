#ifndef __NRF24L01_CON_H__
#define __NRF24L01_CON_H__
	//Typedefs
	typedef struct
	{
		uint8_t		sender_id[5];
		uint16_t	conn_id;
		uint16_t	seq_num;
		uint8_t		challenge;
		uint8_t		response_challenge;
		uint8_t		data_len;
		uint8_t		action;
		uint8_t		data[20];		
	}
	conn_frame_t;
	
	typedef struct
	{
		uint8_t*	data;
		uint32_t	len_data;				
	}
	conn_data_t;
	
	typedef struct  
	{
		conn_frame_t**	tx_frames;	//Buffer of all frames sent via this connection. Allows to replay single packets if requested.
		uint16_t		tx_frame_num;
		conn_data_t**	rx_data;
		uint8_t			rx_payloads;
		uint16_t		frame_num;
		uint16_t		frame_cnt;
		uint8_t			action;
		uint8_t			sender_id[5];
		uint16_t		conn_id;
		uint8_t			seq_num;
		uint8_t			rx_challenge;
		uint8_t			tx_challenge;
		uint8_t			pipe;
		uint16_t		data_len;
		uint16_t		tx_bytes;	//Keeping track of memory usage
		uint16_t		rx_bytes;
	}
	conn_connection_t;
	
	typedef struct
	{
		conn_connection_t*	connection;
		uint8_t				error_code;
	}
	conn_process_result_t;
	
	//Error codes
	#define CONN_ERROR_OK								0
	#define CONN_ERROR_NEW_CONNECTION_SEQUENCE			1
	#define CONN_ERROR_NEW_CONNECTION_ACTION			2
	#define CONN_ERROR_NEW_CONNECTION_NO_MEM			3
	#define	CONN_ERROR_NEW_CONNECTION_DATA_PTR_NO_MEM	4
	#define CONN_ERROR_NEW_CONNECTION_DATA_NO_MEM		5
	#define CONN_ERROR_NEW_PACKET_SEQUENCE				6
	#define CONN_ERROR_NEW_PACKET_CHALLENGE				7
	#define	CONN_ERROR_NEW_PACKET_DATA_PTR_NO_MEM		8
	#define CONN_ERROR_NEW_PACKET_DATA_NO_MEM			9
	#define CONN_ERROR_CONNECTION_SEQUENCE				10
	#define CONN_ERROR_CONNECTION_CHALLENGE				11
	#define CONN_ERROR_CONNECTION_FRAME_TOO_MANY		12
	#define CONN_ERROR_CONNECTION_DATA_FRAME_NO_MEM		13

	//Includes
	#include <stdint.h>
	#include <stdlib.h>
	#include <string.h>
	
	//Config
	#include "config/config.h"
	
	extern void				conn_nrf24l01_irq_hook(uint8_t status);
	extern void				conn_init(void);
	extern void				conn_main(void);
	
	conn_process_result_t*	conn_process_frame(conn_frame_t* frame);
	void					conn_pack_frame(uint8_t* data, conn_frame_t* frame);
	void					conn_unpack_frame(conn_frame_t* frame, uint8_t* data);
	conn_connection_t*		conn_does_connection_exist(uint16_t conn_id);
	conn_connection_t*		conn_create_connection(void);
	conn_connection_t*		conn_remove_connection(conn_connection_t* connection);
	conn_frame_t*			conn_create_frame(void);
	uint8_t					rand_8bit(void);
	void					conn_free_connection(conn_connection_t* connection);
	void					conn_free_frame(conn_frame_t* frame);
	void					conn_free_data(conn_data_t* data);
	void					conn_free_process_result(conn_process_result_t* result);
	
	#include "connection.c"
#endif