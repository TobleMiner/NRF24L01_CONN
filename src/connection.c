unsigned long rand_seed __attribute__ ((section (".noinit")));

uint8_t conn_pipe_available		= 7;
uint8_t conn_num_connections	= 0;

conn_connection_t** conn_connections;

void conn_init()
{
	srand(rand_seed);
}

void conn_nrf24l01_irq_hook(uint8_t status)
{
	if(status & NRF24L01_MASK_STATUS_RX_DR)
	{
		conn_pipe_available = NRF24L01_get_pipe_from_status(status);
	}
}

void conn_main()
{
	if(conn_pipe_available != 7)
	{
		uint8_t len = NRF24L01_get_payload_len(conn_pipe_available);
		if(len == 0)
		{
			uart_write_async("RX: Length of payload is 0");
			return;
		}
		conn_frame_t* frame = conn_create_frame();
		uint8_t* data = malloc(len);
		if(frame == NULL || data == NULL)
		{
			conn_pipe_available = 7;
			NRF24L01_LOW_set_register(NRF24L01_REG_STATUS, NRF24L01_MASK_STATUS_RX_DR); //Clear data-ready flag
			return;
		}
		NRF24L01_get_received_data(data, len);
		conn_pipe_available = 7;
		NRF24L01_LOW_set_register(NRF24L01_REG_STATUS, NRF24L01_MASK_STATUS_RX_DR); //Clear data-ready flag
		conn_pack_frame(data, frame);
		free(data);
		conn_process_result_t* process_result = conn_process_frame(frame);
		conn_free_frame(frame);
		if(process_result == NULL)
		{
			uart_write_async("RX: Too few memory to save result");
		}
		else
		{
			if(process_result->error_code != CONN_ERROR_OK)
			{
				conn_free_connection(process_result->connection);
				uart_send_byte(process_result->error_code);
			}
			free(process_result);
		}
	}
}

conn_process_result_t* conn_process_frame(conn_frame_t* frame)
{
	conn_process_result_t* result = malloc(sizeof(conn_process_result_t));
	if(result == NULL)
		return NULL;
	result->error_code = CONN_ERROR_OK;
	conn_connection_t* connection = conn_does_connection_exist(frame->conn_id);
	if(connection == NULL)
	{
		if(frame->action != 0)
		{
			result->error_code = CONN_ERROR_NEW_CONNECTION_ACTION;
			return result;
		}
		if(frame->seq_num != 0)
		{
			result->error_code = CONN_ERROR_NEW_CONNECTION_SEQUENCE;
			return result;
		}
		connection = conn_create_connection();
		if(connection == NULL)
		{
			result->error_code = CONN_ERROR_NEW_CONNECTION_NO_MEM;
			return result;
		}
		result->connection = connection;
		connection->frame_num	= frame->data[0];
		connection->frame_num	|= frame->data[0] << 8;
		connection->frame_cnt	= 0;
		memcpy(connection->sender_id, frame->sender_id, sizeof(connection->sender_id));
		connection->rx_payloads++;
		connection->rx_data = realloc(connection->rx_data, sizeof(conn_data_t**) * connection->rx_payloads);
		if(connection->rx_data == NULL)
		{
			result->error_code = CONN_ERROR_NEW_CONNECTION_DATA_PTR_NO_MEM;
			return result;
		}
		connection->rx_data[connection->rx_payloads - 1] = malloc(sizeof(conn_data_t));
		if(connection->rx_data[connection->rx_payloads - 1] == 0)
		{
			result->error_code = CONN_ERROR_NEW_CONNECTION_DATA_NO_MEM;
			return result;
		}
		connection->rx_data[connection->rx_payloads - 1]->len_data = 0;
	}
	else
	{
		result->connection = connection;
		if(frame->action == 0) //New data packet
		{
			if(frame->seq_num != 0)
			{
				result->error_code = CONN_ERROR_NEW_PACKET_SEQUENCE;
				return result;
			}
			if(frame->challenge != connection->rx_challenge) //Although this is a new set of data the challenge is kept
			{
				result->error_code = CONN_ERROR_NEW_PACKET_CHALLENGE;
				return result;
			}
			connection->frame_num	= frame->data[0];
			connection->frame_num	|= frame->data[0] << 8;
			connection->frame_cnt	= 0;
			connection->rx_payloads++;
			connection->rx_data = realloc(connection->rx_data, sizeof(conn_data_t**) * connection->rx_payloads);
			if(connection->rx_data == NULL)
			{
				result->error_code = CONN_ERROR_NEW_PACKET_DATA_PTR_NO_MEM;
				return result;
			}
			connection->rx_data[connection->rx_payloads - 1] = malloc(sizeof(conn_data_t));
			if(connection->rx_data[connection->rx_payloads - 1] == 0)
			{
				result->error_code = CONN_ERROR_NEW_PACKET_DATA_NO_MEM;
				return result;
			}
			connection->rx_data[connection->rx_payloads - 1]->len_data = 0;
		}
		else
		{
			if(frame->seq_num == 0)
			{
				result->error_code = CONN_ERROR_CONNECTION_SEQUENCE;
				return result;
			}
			if(frame->seq_num != connection->seq_num + 1)
			{
				result->error_code = CONN_ERROR_CONNECTION_SEQUENCE;
				return result;
			}
			connection->seq_num++;
			if(frame->challenge != connection->rx_challenge)
			{
				result->error_code = CONN_ERROR_CONNECTION_CHALLENGE;
				return result;
			}
			if(connection->frame_cnt >= connection->frame_num) //Invalid, too many packets received.
			{
				result->error_code = CONN_ERROR_CONNECTION_FRAME_TOO_MANY;
				return result;
			}
			connection->frame_cnt++;
			if((frame->action & 128) > 0 && frame->data_len > 0) //Packet has a length of more than 0 bytes and is a data packet
			{
				conn_data_t* data_frame = connection->rx_data[connection->rx_payloads - 1];
				data_frame->data = realloc(data_frame->data, data_frame->len_data + frame->data_len);
				if(data_frame->data == NULL)
				{
					result->error_code = CONN_ERROR_CONNECTION_DATA_FRAME_NO_MEM;
					return result;
				}
				uint8_t* ptr = data_frame->data + data_frame->len_data; //Point to end of rx buffer
				memcpy(ptr, frame->data, frame->data_len); //Append data to rx buffer
				connection->rx_bytes += frame->data_len;
				data_frame->len_data += frame->data_len;
			}
		}
	}
	connection->tx_challenge = frame->response_challenge;
	connection->rx_challenge = rand_8bit();
	return result;
}

//Helper functions
uint8_t rand_8bit()
{
	return rand() % 256;
}

void conn_pack_frame(uint8_t* data, conn_frame_t* frame)
{
	for(uint8_t i = 0; i < sizeof(frame->sender_id); i++)
	{
		frame->sender_id[i] = *data;
		data++;
	}
	frame->conn_id = *data; data++;
	frame->conn_id |= *data << 8; data++;
	frame->seq_num = *data; data++;
	frame->seq_num |= *data << 8; data++;
	frame->challenge = *data; data++;
	frame->response_challenge = *data; data++;
	frame->action = *data; data++;
	if(frame->action >= 128)
	{
		frame->data_len = frame->action - 128;
	}
	for(uint8_t i = 0; i < sizeof(frame->data); i++)
	{
		frame->data[i] = *data;
		data++;
	}
}

void conn_unpack_frame(conn_frame_t* frame, uint8_t* data)
{
	for(uint8_t i = 0; i < sizeof(frame->sender_id); i++)
	{
		*data = frame->sender_id[i];
		data++;
	}
	*data = frame->conn_id; data++;
	*data = frame->conn_id >> 8 & 0xFF; data++;
	*data = frame->seq_num; data++;
	*data = frame->seq_num >> 8 & 0xFF; data++;
	*data = frame->challenge; data++;
	*data = frame->response_challenge; data++;
	uint8_t action = frame->action;
	if(action == 128)
		action += frame->data_len;
	*data = action; data++;
	for(uint8_t i = 0; i < sizeof(frame->data); i++)
	{
		*data = frame->data[i];
		data++;
	}
}

conn_connection_t* conn_does_connection_exist(uint16_t conn_id)
{
	for(uint8_t i = 0; i < conn_num_connections; i++)
	{
		conn_connection_t* connection = conn_connections[i];
		if(connection->conn_id)
		{
			return connection;
		}
	}
	return NULL;
}

conn_frame_t* conn_create_frame()
{
	conn_frame_t* frame = malloc(sizeof(conn_frame_t));
	if(frame == NULL)
		return NULL;
	frame->action				= 0;
	frame->challenge			= 0;
	frame->conn_id				= 0;
	frame->data_len				= 0;
	frame->response_challenge	= 0;
	frame->seq_num				= 0;
	return frame;
}

conn_connection_t* conn_create_connection()
{
	if(conn_num_connections >= CONN_MAX_CONNECTIONS) //All data pipes in use. Check back later ^^
		return NULL;
	conn_connection_t* connection = malloc(sizeof(conn_connection_t));
	if(connection == NULL) //No more memory available
		return NULL;
	conn_connection_t** conn_connections_new = malloc(sizeof(conn_connection_t*) * (conn_num_connections + 1)); //Create new connection buffer
	if(conn_connections_new == NULL) //No more memory available
		return NULL;
	if(conn_num_connections > 0)
	{
		memcpy(conn_connections_new, conn_connections, sizeof(conn_connection_t*) * conn_num_connections);
		free(conn_connections);
	}
	conn_connections_new[conn_num_connections] = connection;
	conn_connections = conn_connections_new;
	conn_num_connections++;
	connection->action			= 0;
	connection->conn_id			= 0;
	connection->data_len		= 0;
	connection->frame_cnt		= 0;
	connection->frame_num		= 0;
	connection->rx_challenge	= 0;
	connection->tx_challenge	= 0;
	connection->rx_bytes		= 0;
	connection->tx_bytes		= 0;
	connection->rx_payloads		= 0;
	connection->seq_num			= 0;
	connection->pipe			= 0;
	connection->rx_data			= NULL;
	connection->tx_frames		= NULL;
	return connection;
}

conn_connection_t* conn_remove_connection(conn_connection_t* connection)
{
	if(conn_num_connections <= 0)
		return NULL;
	conn_connection_t**	conn_connections_new = malloc(sizeof(conn_connection_t*) * (conn_num_connections - 1)); //Create new connection buffer
	if(conn_connections_new == NULL) //No more memory available
		return NULL;
	conn_connection_t**	ptr = conn_connections_new;
	conn_connection_t*	con_ptr = NULL;
	for(uint8_t i = 0; i < conn_num_connections; i++)
	{
		if(conn_connections[i] != connection)
		{
			*ptr = conn_connections[i];
			ptr++;
		}
		else
			con_ptr = connection;
	}
	free(conn_connections);
	conn_connections = conn_connections_new;
	conn_num_connections--;
	return con_ptr;
}

void conn_free_connection(conn_connection_t* connection)
{
	uint8_t i = 0;
	for(i = 0; i < connection->tx_frame_num; i++)
	{
		conn_free_frame(connection->tx_frames[i]);
	}
	free(connection->tx_frames);
	for(i = 0; i < connection->rx_payloads; i++)
	{
		conn_free_data(connection->rx_data[i]);
	}
	free(connection->rx_data);
	free(connection);
}

void conn_free_frame(conn_frame_t* frame)
{
	free(frame);
}

void conn_free_data(conn_data_t* data)
{
	free(data->data);
	free(data);
}

void conn_free_process_result(conn_process_result_t* result)
{
	free(result);
}