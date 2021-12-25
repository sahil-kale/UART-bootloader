#include "etx_ota_update.h"
#include "main.h"
#include "string.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

//Rx Buffer to hold data
static uint8_t Rx_buffer[ETX_OTA_PACKET_MAX_SIZE] = {0};
//Current OTA state
static ETX_OTA_STATE_ ota_state = ETX_OTA_STATE_IDLE;

static uint32_t ota_fw_total_size = 0; //Total size of the firmware
static uint32_t ota_fw_crc = 0; //CRC of the firmware image
static uint32_t ota_fw_received_size = 0; //Size of the firmware image received

/**
 * @brief Receive chunk of data from the host
 * @param data Pointer to the data
 * @param max_len Maximum length of the data
 */
static uint16_t etx_receive_chunk(uint8_t *data, uint16_t max_len);

/**
 * @brief Process received data according to the frame command
 * @param buf Pointer to the data
 * @param len Length of the data
 */
static ETX_OTA_EX_ etx_process_data(uint8_t *buf, uint16_t len);

/**
 * @brief send ota response to host
 * @param response response code
 */
static void etx_ota_send_response(uint8_t response);

/**
 * @brief Write data to the flash
 * @param data Pointer to the data
 * @param data_len Length of the data
 * @param is_first_block indicates if this is the first block of the firmware
 */
static HAL_StatusTypeDef write_data_to_flash_app(uint8_t *data, uint16_t data_len, bool is_first_block);

ETX_OTA_EX_ etx_ota_download_and_flash(void)
{
    ETX_OTA_EX_ ret = ETX_OTA_EX_OK;
    uint16_t len = 0;

    printf("Waiting for OTA data\n");

    /* Resetting the variables */
    ota_fw_total_size = 0u;
    ota_fw_received_size = 0u;
    ota_fw_crc = 0u;
    ota_state = ETX_OTA_STATE_START;

    //Main process loop:
    do
    {
        memset(Rx_buffer, 0, sizeof(Rx_buffer)); //Clear rx buffer

        len = etx_receive_chunk(Rx_buffer, sizeof(Rx_buffer)); //Receive data from host

        if(len != 0)
        {
            ret = etx_process_data(Rx_buffer, len); //Process received data
        }
        else
        {
            ret = ETX_OTA_EX_ERR;
        }

        if(ret != ETX_OTA_EX_OK)
        {
            printf("Sending NACK\n");
            etx_ota_send_response(ETX_OTA_NACK);
            break;
        }
        else
        {
            printf("Sending ACK\n");
            etx_ota_send_response(ETX_OTA_ACK);
        }

    } while (ota_state != ETX_OTA_STATE_IDLE);
    
    return ret;

}

static ETX_OTA_EX_ etx_process_data(uint8_t *buf, uint16_t len)
{
    ETX_OTA_EX_ ret = ETX_OTA_EX_ERR;

    do
    {
        //Main loop to process received data
        if(!buf || !len) //Assert that the data is valid
        {
            break;
        }
        
        ETX_OTA_COMMAND_ *cmd = (ETX_OTA_COMMAND_ *)buf; //Cast the data to the command struct
        if(cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD)
        {
            if(cmd->cmd = ETX_OTA_CMD_ABORT)
            {
                break;
            }
        }

        switch(ota_state) //OTA state machine
        {
            case ETX_OTA_STATE_IDLE:
                printf("ETX_OTA_STATE_IDLE\n");
                ret = ETX_OTA_EX_OK;
                break;

            case ETX_OTA_STATE_START:
                if(cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD && cmd->cmd == ETX_OTA_CMD_START)
                {
                    printf("Starting OTA!\n");
                    ota_state = ETX_OTA_STATE_HEADER;
                    ret = ETX_OTA_EX_OK;
                }
                break;
            
            case ETX_OTA_STATE_HEADER:
                ETX_OTA_HEADER_ *header = (ETX_OTA_HEADER_ *)buf; //Cast the data to the header struct
                if(header->packet_type == ETX_OTA_PACKET_TYPE_HEADER)
                {
                    ota_fw_total_size = header->meta_data.package_size;
                    ota_fw_crc = header->meta_data.package_crc;
                    ota_state = ETX_OTA_STATE_DATA;
                    ret = ETX_OTA_EX_OK;
                    printf("Received OTA Header. FW Size = %ld\r\n", ota_fw_total_size);
                }
                break;

            case ETX_OTA_STATE_DATA:
                ETX_OTA_DATA_ *data = (ETX_OTA_DATA_ *)buf; //Cast the data to the data struct
                uint16_t data_len = data->data_len;
                HAL_StatusTypeDef ex;

                if(data->packet_type == ETX_OTA_PACKET_TYPE_DATA)
                {
                    ex = write_data_to_flash_app(buf, data_len, (ota_fw_received_size == 0));
                    if(ex = HAL_OK)
                    {
                        printf("[%ld/%ld]\r\n", ota_fw_received_size/ETX_OTA_DATA_MAX_SIZE, ota_fw_total_size/ETX_OTA_DATA_MAX_SIZE);
                        if(ota_fw_received_size >= ota_fw_total_size)
                        {
                            //received full data
                            ota_state = ETX_OTA_STATE_END;
                        }
                    }
                    ret = ETX_OTA_EX_OK;
                }
                break;

            case ETX_OTA_STATE_END:
                ETX_OTA_COMMAND_ *cmd = (ETX_OTA_COMMAND_ *)buf; //Cast the data to the command struct
                if(cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD && cmd->cmd == ETX_OTA_CMD_END)
                {
                    printf("Received OTA End\r\n");
                    ota_state = ETX_OTA_STATE_IDLE;
                    ret = ETX_OTA_EX_OK;
                }

                break;

            default:
                //We darn screwed up if we're here lol
                ret = ETX_OTA_EX_ERR;
                break;
        }
    } while (false);
    

    return ret;

}

static uint16_t etx_receive_chunk(uint8_t *buf, uint16_t max_len)
{
    int16_t ret = 0;
    uint16_t index = 0;
    uint16_t data_len = 0;
    do
    {
        //receive SOF
        ret = HAL_UART_Receive(&huart2, &buf[index], 1, HAL_MAX_DELAY); 
        if(ret != HAL_OK)
        {
            break;
        }

        if(buf[index] != ETX_OTA_SOF)
        {
            ret = ETX_OTA_EX_ERR;
            break; //SOF was not received
        }
        
        index++; //Increment the index

        //Get packet type
        ret = HAL_UART_Receive(&huart2, &buf[index], 1, HAL_MAX_DELAY);
        if(ret != HAL_OK)
        {
            break; //Error receiving packet type
        }

        index++; //Increment the index

        //Get data length
        ret = HAL_UART_Receive(&huart2, &buf[index], 2, HAL_MAX_DELAY);
        if(ret != HAL_OK)
        {
            break; //Error receiving data length
        }

        data_len = ((uint16_t)buf[index] << 8) | buf[index+1];
        index += 2; //Increment the index

        for(uint16_t i = 0; i < data_len; i++)
        {
            ret = HAL_UART_Receive(&huart2, &buf[index], 1, HAL_MAX_DELAY);
            if(ret != HAL_OK)
            {
                break; //Error receiving data
            }

            index++; //Increment the index
        }

        //Get CRC
        ret = HAL_UART_Receive(&huart2, &buf[index], 4, HAL_MAX_DELAY);
        if(ret != HAL_OK)
        {
            break; //Error receiving CRC
        }

        index += 4; //Increment the index

        //TO DO: Check CRC

        //Get EOF
        ret = HAL_UART_Receive(&huart2, &buf[index], 1, HAL_MAX_DELAY);
        if(ret != HAL_OK)
        {
            break; //Error receiving EOF
        }

        if(buf[index] != ETX_OTA_EOF)
        {
            ret = ETX_OTA_EX_ERR;
            break; //EOF was not received
        }

    } while (false);

    if(max_len < index)
    {
        printf("Received more data than expected. Expected = %d, Received = %d\r\n",
                                                              max_len, index );
    }

    return index;
    
}

static void etx_ota_send_resp(uint8_t type)
{
    ETX_OTA_RESP_ rsp =
    {
        .sof = ETX_OTA_SOF,
        .packet_type = ETX_OTA_PACKET_TYPE_RESPONSE,
        .data_len = 1,
        .status = type,
        .crc = 0, //TODO: Add CRC
        .eof = ETX_OTA_EOF,
    };

    //Send the response
    HAL_UART_Transmit(&huart2, (uint8_t *)&rsp, sizeof(rsp), HAL_MAX_DELAY);
}

static HAL_StatusTypeDef write_data_to_flash_app(uint8_t *data, uint16_t data_len, bool is_first_block)
{
    HAL_StatusTypeDef ret = {0};
    do
    {
        ret = HAL_FLASH_Unlock();
        if(ret != HAL_OK)
        {
            break;
        }

        //If this is the first time, we need to erase the flash.
        if(is_first_block)
        {
            printf("Erasing flash\n");

            FLASH_EraseInitTypeDef EraseInitStruct = {0};

            //Erase the flash
            EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
            EraseInitStruct.Sector = FLASH_SECTOR_5; //This is defined by the requirements we set
            EraseInitStruct.NbSectors = 2; //Sectors 5 & 6
            EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3; //WTF is this for??

            uint32_t SectorError = 0;
            ret = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);
            if(ret != HAL_OK)
            {
                break;
            }

        }

        //Write the data to the flash
        for(uint16_t i = 0; i < data_len; i++)
        {
            ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, (uint32_t)(APP_STACK_START_ADDR + ota_fw_received_size), data[4+i]);
            if(ret == HAL_OK)
            {
                ota_fw_received_size++;
            }
            else
            {
                printf("Flash Wrire Error\n");
                break;
            }
        }

        if(ret != HAL_OK)
        {
            break;
        }
        
        ret = HAL_FLASH_Lock();
        if(ret != HAL_OK)
        {
            break;
        }


    } while (false);

    return ret;
    
}
