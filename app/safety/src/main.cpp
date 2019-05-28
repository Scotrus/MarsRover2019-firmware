#include "mbed.h"
#include "rover_config.h"
#include "CANMsg.h"

const unsigned int  RX_ID = ROVER_SAFETY_CANID; 
const unsigned int  TX_ID = ROVER_JETSON_CANID + 30; 
const unsigned int  CAN_MASK = ROVER_CANID_FILTER_MASK;

I2C 				i2c(I2C_SDA, I2C_SCL);
Serial              pc(SERIAL_TX, SERIAL_RX, ROVER_DEFAULT_BAUD_RATE);
CAN                 can(CAN_RX, CAN_TX, ROVER_CANBUS_FREQUENCY);
CANMsg              txMsg;

DigitalOut          ledErr(LED1);
DigitalOut			ledI2C(LED3);
DigitalOut          ledCAN(LED4);
AnalogIn            battery(V_MONITOR);

//Sensor Address Indices
enum {
	sensor_100A1 = 0, 
	sensor_100A2, 
	sensor_30A
};

const int ADC_address[3] = {0x54 << 1, 0x50 << 1, 0x61 << 1};
 
#define TOTAL_SAMPLE 10
char adc_sample[2]; 
float raw_adc_sum = 0.0;
float raw_adc[3];
float current[3];

// Specfic variables for 100A sensor
const float lowerV_100A = 0.314; //lowerV and upperV is the voltage after signal has been amplified
const float upperV_100A = 3.218;
const float ampRange_100A = 100;
const float bitsPerVolt_100A = 255/3.3;
const int bitRange_100A = round((upperV_100A - lowerV_100A) * bitsPerVolt_100A); // 224.4
const float iConversion_100A = bitRange_100A/ampRange_100A; // 2.244

// Specfic variables for 30A sensor
const float lowerV_30A = 0.385714; //lowerV and upperV is the voltage after signal has been amplified
const float upperV_30A = 4.785714;
const float ampRange_30A = 30;
const float bitsPerVolt_30A = 255/5;
const int bitRange_30A = round((upperV_30A - lowerV_30A) * bitsPerVolt_30A);
const float iConversion_30A = bitRange_30A/ampRange_30A;

// Voltage monitoring specific
const float compression_factor = 0.110629;
const int cell_num = 6;
const float full_cell = 4.20;
const float low_cell = 3.75; // TODO verify this value
const float full_bat = full_cell * cell_num * compression_factor/3.3;
const float low_bat = low_cell * cell_num * compression_factor/3.3;
float bat_value = 0;
#define V_INDEX 3

void initCAN() {
    can.filter(RX_ID, ROVER_CANID_FILTER_MASK, CANStandard);

    // for (int canHandle = firstCommand; canHandle <= lastCommand; canHandle++) {
    //     can.filter(RX_ID + canHandle, 0xFFF, CANStandard, canHandle);
    // }
}

int main() {
	pc.printf("Program Started\r\n\r\n");
	
	i2c.frequency(100000);
    initCAN();
	ledI2C = 1;
	ledCAN = 1;

    while(1) {
		raw_adc_sum = 0.0;
		
        /* avgReading -= avgReading/sampleSize;
           avgReading += newReading/sampleSize
        */
		// Take multiple samples of each sensor
		for(int i = 0; i < 3; i++) {			
			for (int j = 0; j < TOTAL_SAMPLE; j++) { 
				i2c.read(ADC_address[i], adc_sample, 2, false);
				raw_adc_sum = raw_adc_sum + ((adc_sample[0] << 4) + (adc_sample[1] >> 4));
				ledI2C = !ledI2C;
			}
			raw_adc[i] = raw_adc_sum / TOTAL_SAMPLE;
			if (i == sensor_100A1) {
				//current[i] = ((bitRange_100A - raw_adc[i])/iConversion_100A);
				current[i] = ((246 - raw_adc[i])/iConversion_100A) - 16.5;
			}
			else if(i == sensor_100A2) {
				current[i] = ((246 - raw_adc[i])/iConversion_100A) - 1;
			}
			else {
				current[i] = ((bitRange_30A - raw_adc[i])/iConversion_30A);
			}
			pc.printf("Address: %d\tCurrent Sensor %d: %f\r\n", ADC_address[i] >> 1, i, current[i]);
			
			// Send current data over CAN
			uint8_t data = round(current[i] * 100); // Convert current data to int 
			CANMsg txMsg(TX_ID + i);
			txMsg << data;
			ledCAN = !ledCAN;

			if(can.write(txMsg)) {
				pc.printf("Sucessful CAN transmission\r\n");
			}
			else {
				pc.printf("Error in CAN transmission\r\n");
				ledErr = 1;
			}
		}

        // Reading of battery voltage
        /* Battery voltage conversion 
        * Voltage divider of 82k and 10.2k provide a compression factor of 0.110629
        * Does CAN Msg need to be sent out?
        */
       bat_value = battery.read();
       pc.printf("Battery Level: %f\r\n", bat_value);
       if (bat_value < low_cell){
           CANMsg txMsg(TX_ID + V_INDEX);
           pc.printf("REPLACE BATTERY, Battery Level LOW!!! \r\n");
       }
		wait(1);
    }
}
