import serial

ser = serial.Serial('COM9', 115200)
print(ser.name)

try: 
    while True:
        print(ser.readline())
except KeyboardInterrupt:
    exit()