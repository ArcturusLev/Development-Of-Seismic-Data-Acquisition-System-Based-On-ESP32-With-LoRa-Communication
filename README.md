This project purpose is to make an alternative seismic data acquisition with low build cost and have portability to do wireless communication using LoRa. This project detect seismic signal within 1 - 100 hz optimal range frequency.
This device have 2 main process configured in microcontroller which is acquisition data and transmitting data to wireless communication LoRa. 
Acquisition data collect the data sample 833 sample per second and store the data in data logger with same amount of sample.
**================SPECIFICATION================
Processor: ESP32 Wroom S3 
ADC: ADS1115
Data Logger: Flash Memory or SD Card
Wireless Communication: LoRa (E220-900t22d)**

WORKFLOW
1. Data Acquisition
   The Outline of the data acquisition workflow broadly convert analog signal especially seismic signal into digital signal using external ADC. First data from seismic sensor will input on analog input in the ADS1115 IC, the chosen analog input in this project is AIN1. After data collected from seismic sensor the data will be converted by ADC into digital value with 16-bit resolution. ADC will transfer this data into the microcontroller with I2C communication protocol. At the time the raw data is transfered into microcontroller data will process. First task is to convert raw digital data into voltage parameter, and final data is transferred into several place which is Serial Monitor, Data Logger, and LoRa.
2. Data Logger
