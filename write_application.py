# This application is NOT part of the tutorial. It is my own code to interface with the bootloader.
# Based off the example RS232 interface code.

# Flow -> Get COM port & filepath to application binary
# Then -> Open COM port and send the file to the bootloader as per packet definition
import serial
import struct
import os

COMPORT = r'COM4'
BAUD_RATE = 115200
APP_BINARY_FILEPATH = r'app_blinky/Debug/app_blinky.bin'
serialObject = 0
fileObject = 0
binary_size = 0
file_bytes_pointer = 0 #Counts the bytes read
DEFAULT_BYTE_READ = 512

"""DEFINITIONS"""
PACKET_SOF_CODE = 0xAA
PACKET_EOF_CODE = 0xBB

PACKET_TYPE_CMD = 0x00
PACKET_TYPE_DATA = 0x01
PACKET_TYPE_HEADER = 0x02
PACKET_TYPE_RESPONSE = 0x03

"""Response Struct
 * __________________________________________
 * |     | Packet |     |        |     |     |
 * | SOF | Type   | Len | Status | CRC | EOF |
 * |_____|________|_____|________|_____|_____|
 *   1B      1B     2B      1B     4B    1B
"""
RESPONSE_ACK_CODE = 0x00
RESPONSE_NACK_CODE = 0x01
response_struct_code = '>BBHBIB'

def is_ack_response_received():
    return True
    RESPONSE_LEN_BYTES = struct.calcsize(response_struct_code)
    response = serialObject.read(RESPONSE_LEN_BYTES)
    sof, pkt_type, len, status, crc, eof = struct.unpack(response_struct_code, response)

    #TODO: Add CRC check
    
    if pkt_type == 0x06:
        print("ACK received")
        if(status == RESPONSE_ACK_CODE):
            return True
        

def generate_command_packet(cmd_type: int):
    """ 
    * OTA Command format
    *
    * ________________________________________
    * |     | Packet |     |     |     |     |
    * | SOF | Type   | Len | CMD | CRC | EOF |
    * |_____|________|_____|_____|_____|_____|
    *   1B      1B     2B    1B     4B    1B
    """
    packet = bytearray()
    packet.append(PACKET_SOF_CODE)
    packet.append(PACKET_TYPE_CMD)
    packet.append(0x00) # Length
    packet.append(0x01) # Length part 2
    packet.append(cmd_type)
    append_crc(packet)
    packet.append(PACKET_EOF_CODE)
    return packet

def send_ota_start():
    print("Sending OTA start command")
    CMD_TYPE_START = 0x00
    cmd_packet = generate_command_packet(CMD_TYPE_START)
    serialObject.write(cmd_packet)
    assert(is_ack_response_received())

def send_ota_end():
    print("Sending OTA end command")
    CMD_TYPE_END = 0x01
    cmd_packet = generate_command_packet(CMD_TYPE_END)
    serialObject.write(cmd_packet)
    assert(is_ack_response_received())    

def send_ota_header(filepath: str):
    """
    * OTA Header format
    *
    * __________________________________________
    * |     | Packet |     | Header |     |     |
    * | SOF | Type   | Len |  Data  | CRC | EOF |
    * |_____|________|_____|________|_____|_____|
    *   1B      1B     2B     16B     4B    1B
    """
    header_packet = bytearray()
    header_packet.append(PACKET_SOF_CODE)
    header_packet.append(PACKET_TYPE_HEADER)
    meta_info = produce_meta_info(filepath)
    header_packet.append(len(meta_info)) #Append data length
    header_packet.extend(meta_info)
    append_crc(header_packet)
    header_packet.append(PACKET_EOF_CODE)
    assert(is_ack_response_received())

def send_ota_data(data_bytes: bytearray):
    print("Sending OTA data")
    data_packet = bytearray()
    data_packet.append(PACKET_SOF_CODE)
    data_packet.append(PACKET_TYPE_DATA)


    #data_packet.append(0x00) # Length
    #data_packet.append(0x00) # Length part 2
        #data_packet.extend(data)
        #append_crc(data_packet)

def produce_meta_info(filepath: str) -> bytearray:
    """
    uint32_t package_size;
    uint32_t package_crc;
    uint32_t reserved1;
    uint32_t reserved2;
    """
    meta_info = bytearray()
    #Determine Binary Size
    file_size = int(os.stat(filepath).st_size)
    global binary_size
    binary_size = file_size
    size_info = file_size.to_bytes(4, byteorder='big')
    for byte in size_info:
        meta_info.append(byte) #Append the 32 bit size into the meta info
    append_crc(meta_info)
    for i in range(0, 8):
        meta_info.append(0x00) #Padding    
    return meta_info

def append_crc(packet: bytearray):
    for i in range (0, 4):
        packet.append(0x00) #CRC

def init_serial_port():
    global serialObject
    serialObject = serial.Serial(COMPORT, BAUD_RATE)
    print('Serial port opened: ' + serialObject.name)

if __name__ == '__main__':
    init_serial_port()
    send_ota_start()
    send_ota_header(APP_BINARY_FILEPATH)
    binary_file = open(APP_BINARY_FILEPATH, 'rb')
    #print(binary_file.read(DEFAULT_BYTE_READ))
    while (file_bytes_pointer < binary_size):
        data = binary_file.read(DEFAULT_BYTE_READ)
        file_bytes_pointer += len(data)
        print(data)
        #send_ota_data(data)

"""PICKUP NOTES:
- Implemented chunking, need to do implement rest of sending packet logic and then we should be good
- Note: the ack is just hardcoded to be true
- Need to implement CRC check
"""

