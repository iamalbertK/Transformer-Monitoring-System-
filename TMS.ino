#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SoftwareSerial.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Arduino.h>
#include <ZMPT101B.h>
#include <ACS712.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

const char* ssid = "";
const char* password = "";
const char* transformer_id = "T-1001";
const char* phone_number = "";

// Your Domain name with URL path or IP address with path
const char* serverName = "";

// SMS limits
const int MAX_MESSAGES = 3;
const unsigned long COOLDOWN_PERIOD = 0.1 * 60 * 60 * 1000; // 10 minutes in milliseconds

// EEPROM addresses for message counts and timestamps
const int COUNT_START_ADDRESS = 20;
const int TIMESTAMP_START_ADDRESS = 40;

// Number of parameters we're monitoring
const int NUM_PARAMETERS = 8;

unsigned long lastTime = 0;
unsigned long timerDelay = 5000; // 5 seconds

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ULTRASONIC SENSOR CODE
#define TRIG_PIN 12
#define ECHO_PIN 14

// DS18B20 Data wire is connected to GPIO4
#define ONE_WIRE_BUS 4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Variables for Voltage sensor
 
#define SENSITIVITY 500.0f

// ZMPT101B sensor output connected to analog pin 34
// and the voltage source frequency is 50 Hz.
ZMPT101B voltageSensor(34, 50.0);

// Variables for Current sensor
long lastSample = 0;
long sampleSum = 0;
int sampleCount = 0;
float voltage_per_count = 1.220703125;

#define AC_LOAD_CURRENT_SENSOR_PIN 35

void setup() {
  // Initialize the serial communication
  Serial.begin(115200);

  // Start the Wi-Fi connection
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  // Wait for the connection to establish
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }

  // Print the IP address once connected
  Serial.println("Connected to WiFi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  EEPROM.begin(512); // Initialize EEPROM with size

  // Initialize EEPROM values if not set
  for (int i = 0; i < NUM_PARAMETERS; i++) {
    if (EEPROM.read(COUNT_START_ADDRESS + i) == 255) {
      EEPROM.write(COUNT_START_ADDRESS + i, 0);
    }
    unsigned long timestamp;
    EEPROM.get(TIMESTAMP_START_ADDRESS + (i * sizeof(unsigned long)), timestamp);
    if (timestamp == 0xFFFFFFFF) {
      EEPROM.put(TIMESTAMP_START_ADDRESS + (i * sizeof(unsigned long)), 0UL);
    }
  }
  EEPROM.commit();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  sensors.begin();

  analogReadResolution(12);  // Set ADC resolution to 12 bits (0-4095) 

  voltageSensor.setSensitivity(SENSITIVITY);  

  Serial.println("REMOTE TRANSFORMER MONITORING");
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("TRANSFORMER");
  lcd.setCursor(0,1);
  lcd.print("MONITORING");
  lcd.setCursor(11,1);
  lcd.print("SYS.");
  delay(2000);
  lcd.clear();
}

void sendSMS(String message, int parameterIndex) {
  int countAddress = COUNT_START_ADDRESS + parameterIndex;
  int timestampAddress = TIMESTAMP_START_ADDRESS + (parameterIndex * sizeof(unsigned long));
  
  int messageCount = EEPROM.read(countAddress);
  unsigned long lastMessageTime;
  EEPROM.get(timestampAddress, lastMessageTime);
  
  unsigned long currentTime = millis();
  
  if (messageCount < MAX_MESSAGES || (currentTime - lastMessageTime) > COOLDOWN_PERIOD) {
    if ((millis() - lastTime) > timerDelay) {
      // Check WiFi connection status
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure(); // Set insecure to ignore certificate validation

        HTTPClient http;

        // Your Domain name with URL path or IP address with path
        http.begin(client, serverName);

        // Specify content-type header
        http.addHeader("Content-Type", "application/json");

        // Data to send with HTTP POST
        String httpRequestData = "{\"message\":\"" + message + "\",\"transformer_id\":\"" + transformer_id + "\", \"phone\":\"" + phone_number + "\"}";
        
        // Send HTTP POST request
        int httpResponseCode = http.POST(httpRequestData);

        if (httpResponseCode > 0) {
          String response = http.getString();
          Serial.println("Response: " + response);
        } else {
          Serial.print("Error on sending POST: ");
          Serial.println(httpResponseCode);
        }

        if (httpResponseCode == 200) {
          // Update message count and timestamp
          if (messageCount < MAX_MESSAGES) {
            EEPROM.write(countAddress, messageCount + 1);
          } else {
            EEPROM.write(countAddress, 1); // Reset count after cooldown
          }
          EEPROM.put(timestampAddress, currentTime);
          EEPROM.commit();
        }

        // Free resources
        http.end();
      } else {
        Serial.println("WiFi Disconnected");
      }
      lastTime = millis();
    }
  } else {
    Serial.println("Message limit reached or cooldown period not over for parameter " + String(parameterIndex));
  }
}

void loop() {
  
  // Clear the TRIG_PIN by setting it LOW
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  
  // Trigger the ultrasonic pulse
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Read the duration of the echo pulse
  long duration = pulseIn(ECHO_PIN, HIGH);
  
  // Calculate the distance (in cm)
  int oilLevel = duration * 0.034 / 2;
  
  Serial.print("Oil Level: ");
  Serial.print(oilLevel);
  Serial.println(" cm");
  
  // Conditions for Oil Level
  if (oilLevel < 9) {
    sendSMS("Alert: Oil Level is too high at the transformer at GPS Location, {location}", 0);
  }

  if (oilLevel > 19) {
    sendSMS("Alert: Oil Level is too low at the transformer at GPS Location, {location}", 1);
  }

  sensors.requestTemperatures();
  float tempCOil = sensors.getTempCByIndex(0);
  float tempCWinding = sensors.getTempCByIndex(1);

  Serial.print("Temperature of Oil: ");
  Serial.print(tempCOil);
  Serial.print(" °C");
  
  // Conditions for Oil Temperature
  if (tempCOil > 105) {
    sendSMS("Alert: Oil Temperature is too high at the transformer at GPS Location, {location}", 2);
  }

  if (tempCOil < 30) {
    sendSMS("Alert: Oil Temperature is too low at the transformer at GPS Location, {location}", 3);
  }

  Serial.print("  |  Temperature of Winding: ");
  Serial.print(tempCWinding);
  Serial.println(" °C");
  
  // Conditions for Winding Temperature
  if (tempCWinding > 110) {
    sendSMS("Alert: Winding Temperature is too high at the transformer at GPS Location, {location}", 4);
  }

  if (tempCWinding < 30) {
    sendSMS("Alert: Winding Temperature is too low at the transformer at GPS Location, {location}", 5);
  }
  
  // Calculating for Voltage Value
 float VoltageValue = voltageSensor.getRmsVoltage();
  if (VoltageValue < 100){
    VoltageValue = 0;
  }
  Serial.print("Voltage = ");
  Serial.print(VoltageValue);
  Serial.println(" V");

  // Conditions for Voltage Value
  if (VoltageValue > 245) {
    sendSMS("Alert: Secondary voltage Level is too high at the transformer at GPS Location, {location}", 6);
  }

  if (VoltageValue < 220) {
    sendSMS("Alert: Secondary voltage Level is too low at the transformer at GPS Location, {location}", 6);
  }

  // Calculating for Current Value
  if (millis() > lastSample + 1) {
    sampleSum += sq(analogRead(AC_LOAD_CURRENT_SENSOR_PIN) - 3000);
    sampleCount++;
    lastSample = millis();
  }

  float currentValue = 0;
  if (sampleCount == 1000) {
    float mean = sampleSum / 1000.0;
    float value = sqrt(mean);
    float voltage = value * voltage_per_count;
    currentValue = voltage / 66.0;
    sampleSum = 0;
    sampleCount = 0;
  }

  Serial.print("Current = ");
  Serial.print(currentValue );
  Serial.println("A");

  // Conditions for Current Level
  if (currentValue > 28) {
    sendSMS("Alert: Primary current Level is too High at the transformer at GPS Location, {location}", 7);
  }

  if (currentValue < 25) {
    sendSMS("Alert: Primary current Level is too Low at the transformer at GPS Location, {location}", 7);
  }

  // LCD PRINTING
  // Temperature of Winding
  lcd.setCursor(0,0);
  lcd.print("Wind Temp:");
  lcd.setCursor(10,0);
  lcd.print(tempCWinding);
  lcd.setCursor(15,0);
  lcd.print("C");

  // Temperature of Oil
  lcd.setCursor(0,1);
  lcd.print("Oil Temp:");
  lcd.setCursor(10,1);
  lcd.print(tempCOil);
  lcd.setCursor(14,1);
  lcd.print("C");
  delay(5000);
  lcd.clear();

  // Oil Level
  lcd.setCursor(0,0);
  lcd.print("Oil Level:");
  lcd.setCursor(10,0);
  lcd.print(oilLevel);
  lcd.setCursor(14,0);
  lcd.print("CM");

  // Current
  lcd.setCursor(0, 1);
  lcd.print("Ampere Val:");
  lcd.setCursor(11, 1);
  lcd.print(currentValue);
  lcd.setCursor(15, 1);
  lcd.print("A");
  delay(5000);
  lcd.clear();

  // Voltage
  lcd.setCursor(0, 0);
  lcd.print("Volt. Val:");
  lcd.setCursor(10, 0);
  lcd.print(VoltageValue);
  lcd.setCursor(15, 0);
  lcd.print("V");
  delay(5000);
  lcd.clear();

  // Wait before next reading
  delay(1000);
}
