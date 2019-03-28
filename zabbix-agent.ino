
#include <UIPEthernet.h>
#include <Wire.h>
#include <AM2320.h>
#include <Adafruit_BMP085.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <SoftwareSerial.h>



// Описываем датчик температуры DS18B20
#define POWER_MODE  0 // режим питания ds18b20, 0 - внешнее, 1 - паразитное
OneWire sensDs (6);  // датчик подключен к выводу 6

// Описываем датчик СО2 MH-Z19b
SoftwareSerial MH_Z19B(A0, A1); // A0 - к TX сенсора, A1 - к RX
byte cmd[9] = {0xFF,0x01,0x86,0x00,0x00,0x00,0x00,0x00,0x79}; 
unsigned char response[9];

#define UPDATE_INTERVAL 30000 // снимаем показания с датчиков раз в 30 секунд
unsigned long timestamp;

Adafruit_BMP085 bmp;
AM2320 th_sensor(&Wire);

LiquidCrystal_I2C lcd(0x27,16,2); 


byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; 
byte ip[] = { 10, 71, 0, 242 }; // IP address in local network

EthernetServer server(10050); // Zabbix port

float temperature_am;
float temperature_bmp;
float temperature_ds;
float humidity;
float pressure;
unsigned int co2;

void setup() {
  //Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("ZABBIX SENSOR");
  lcd.setCursor(0,1);
  lcd.print("10.71.0.242");
  delay(1000);
  Wire.begin();
  bmp.begin(); 
  MH_Z19B.begin(9600);
  Ethernet.begin(mac, ip);
  server.begin();
  GetSensorsData(temperature_am, temperature_bmp, temperature_ds, humidity, pressure, co2);
  DisplayOnLCD(temperature_ds, temperature_am, humidity, co2);
  //Serial.println("startup complete");
 
}

unsigned int Get_MHZ19_data() {
  MH_Z19B.write(cmd, 9);
  memset(response, 0, 9);
  MH_Z19B.readBytes(response, 9);
  int i;
  byte crc = 0;
  for (i = 1; i < 8; i++) crc+=response[i];
  crc = 255 - crc;
  crc++;

  if ( !(response[0] == 0xFF && response[1] == 0x86 && response[8] == crc) ) {
    return 0;
  } else {
    unsigned int responseHigh = (unsigned int) response[2];
    unsigned int responseLow = (unsigned int) response[3];
    unsigned int ppm = (256*responseHigh) + responseLow;
    return(ppm);
  
}
}

float Get_DS18B20_Temp() {
  byte bufData[9];     // буфер данных
  float temp;  // измеренная температура
  
  sensDs.reset();  // сброс шины
  sensDs.write(0xCC, POWER_MODE); // пропуск ROM
  sensDs.write(0x44, POWER_MODE); // инициализация измерения
  delay(900);  // пауза 0,9 сек
  sensDs.reset();  // сброс шины
  sensDs.write(0xCC, POWER_MODE); // пропуск ROM 
  sensDs.write(0xBE, POWER_MODE); // команда чтения памяти датчика 
  sensDs.read_bytes(bufData, 9);  // чтение памяти датчика, 9 байтов

  if ( OneWire::crc8(bufData, 8) == bufData[8] ) {  // проверка CRC
    temp =  (float)((int)bufData[0] | (((int)bufData[1]) << 8)) * 0.0625 + 0.03125;
    return temp;   
  }
  else { 
    return 0;
  }    
}

//void DisplayOnLCD(float &t_in, float &t_room, float &h, float &p) {
void DisplayOnLCD(float &t_in, float &t_room, float &h, unsigned int &co2) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("T ");
  lcd.print(t_in);
  lcd.setCursor(8,0);
  lcd.print("T2 ");
  lcd.print(t_room);
  lcd.setCursor(0,1);
  lcd.print("H ");
  lcd.print(h);
  lcd.setCursor(8,1);
 // lcd.print("P ");
 // lcd.print(p);
  lcd.print("CO2 ");
  lcd.print(co2);
  
}

void GetSensorsData(float &t_am , float &t_bmp, float &t_ds, float &h, float &p, unsigned int &co2) {
  th_sensor.Read();
  t_am = th_sensor.cTemp;            //температура AM2320
  t_ds = Get_DS18B20_Temp();         //температура DS18B20
  t_bmp = bmp.readTemperature();     //температура BMP180
  h = th_sensor.Humidity;            //влажность (AM2320)
  p = bmp.readPressure()/133.3;      //давление (BMP180)
  co2 = Get_MHZ19_data();            //содержание СО2 (MH-Z19)
}

void loop() {


  
  if (millis() - timestamp < 0) timestamp=millis(); /* проверка на переход millis через 0 */
  if (millis() - timestamp > UPDATE_INTERVAL) {
    timestamp = millis();
    GetSensorsData(temperature_am, temperature_bmp, temperature_ds, humidity, pressure, co2);
    DisplayOnLCD(temperature_ds, temperature_am, humidity, co2);
  }
  

  size_t size;
  unsigned int i;
  unsigned int datalen;
  char request[20];
  if (EthernetClient client = server.available())
  {

   while((size = client.available()) > 0)
        {
          uint8_t* msg = (uint8_t*)malloc(size+1);
          memset(msg, 0, size+1);
          size = client.read(msg,size);
          
          //Serial.println("received request");
          datalen = msg[5];  //длина данных в 5-м байте пакета
          //Serial.print("data length is ");
          //Serial.println(datalen);

          for (i=0; i<=datalen; i++) {
            request[i] = msg[13 + i];  //запрос начиная с 13-го байта
          }
         free(msg);  
        }
         
          //Serial.print("request is ");
          //Serial.println(request);

                   
           
          if (strcmp(request,"agent.ping") == 0) {
            client.print("ZBXD\x01"); // response header   
            byte responseBytes [] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '1'}; 
            client.write(responseBytes, 9);

          } else if (strcmp(request,"env.co2_ppm") == 0) {
            client.print("ZBXD\x01"); // response header   
            byte responseBytes [] = {(byte) String(co2).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            client.write(responseBytes, 8);
            client.print(co2);
                  
          } else if (strcmp(request,"env.temp") == 0) {
            client.print("ZBXD\x01"); // response header   
            byte responseBytes [] = {(byte) String(temperature_am).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            client.write(responseBytes, 8);
            client.print(temperature_am);
           
            
          } else if (strcmp(request,"env.humidity") == 0 ) {
            client.print("ZBXD\x01"); // response header   
            byte responseBytes [] = {(byte) String(humidity).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            client.write(responseBytes, 8);
            client.print(humidity);
            
            
          } else if (strcmp(request,"env.pressure") == 0 ) {
            client.print("ZBXD\x01"); // response header               
            byte responseBytes [] = {(byte) String(pressure).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            client.write(responseBytes, 8);
            client.print(pressure);
           
            
          } else if (strcmp(request,"env.temp_bmp") == 0 ) {
            client.print("ZBXD\x01"); // response header   
            byte responseBytes [] = {(byte) String(temperature_bmp).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            client.write(responseBytes, 8);
            client.print(temperature_bmp);
            
            
           } else if (strcmp(request,"env.temp_ds") == 0 ) {
             client.print("ZBXD\x01"); // response header   
             byte responseBytes [] = {(byte) String(temperature_ds).length(), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
             client.write(responseBytes, 8);
             client.print(temperature_ds);

          
           } else {
             client.print("ZBXD\x01"); // response header   
             byte responseBytes [] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
             client.write(responseBytes, 8);
             client.print("ZBX_NOTSUPPORTED");
          }
                
          delay(10);
          client.stop();
        }
 
    
  }
