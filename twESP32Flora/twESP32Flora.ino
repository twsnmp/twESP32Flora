#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

// Define constant
#define VERSION "1.0.0"
// Sensor
#define DHTPIN D5                // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22            // DHT22
#define SOIL_MOISTURE_SENSOR A0  // Soil Moisture Sensor
#define RAIN_SENSOR_DIGITAL D1   // Rain Sensor Digital
#define RAIN_SENSOR_ANALOG A2    // Rain Sensor Analog

// LED & Button
#define BOOT D9
#define LED D10

// Const
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define MQTT_TOPIC "twESP32Flora/data"

// Config
Preferences pref;

// Calibration data
int dry_soil;
int wet_soil;
int dry_rain;
int wet_rain;


// MQTT Client
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

// JSON Data
JsonDocument doc;
static char outputtext[256] = "";

// Functions
void checkBootButton();
String getInput(const char *msg, bool require = true);
String getIPAddress();
String getSensorType();
bool getYesNo(const char *msg);
int getAnalogRaw(int io);
float getAnalog(int io, int dry, int wet);
bool connectToWiFi(String ssid, String password);
bool publishToMQTT(String mqtt, int port, String sensor);
void setDHT22Data();
void setBME280Data();
void setSoilMoistureData();
void setRainSensorData();
int getPortNumber(int def);
int getInterval(int def);


void setup() {
  Serial.begin(115200);
  Serial.printf("twESP32Flora %s\n", VERSION);
  pinMode(BOOT, INPUT_PULLUP);  // BUTTON
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  delay(100);
  Serial.println("setup start");
  checkBootButton();
  pref.begin("twESP32Flora", false);
  bool hasConfig = pref.getBool("config", false);
  if (!hasConfig) {
    String ssid_in = getInput("enter ssid:");
    String password_in = getInput("enter password:", false);
    String mqtt_in = getIPAddress();
    int port_in = getPortNumber(1883);
    int interval_in = getInterval(60);
    String sensor_in = getSensorType();
    bool rain_in = getYesNo("Has rain sensor? (yes/no):");
    // Calibration
    getInput("Prepare for calibration. Remove the soil moisture sensor from the soil and dry it. Then press Enter.", false);
    int dry_soil_in = getAnalogRaw(SOIL_MOISTURE_SENSOR);
    getInput("Place the soil moisture sensor in water, then press Enter.", false);
    int wet_soil_in = getAnalogRaw(SOIL_MOISTURE_SENSOR);
    pref.putInt("dry_soil", dry_soil_in);
    pref.putInt("wet_soil", wet_soil_in);
    if (rain_in) {
      getInput("Dry the rain sensor, then press Enter.", false);
      int dry_rain_in = getAnalogRaw(RAIN_SENSOR_ANALOG);
      getInput("Drop water on the rain sensor, then press Enter.", false);
      int wet_rain_in = getAnalogRaw(RAIN_SENSOR_ANALOG);
      pref.putInt("dry_rain", dry_rain_in);
      pref.putInt("wet_rain", wet_rain_in);
    }
    pref.putString("ssid", ssid_in);
    pref.putString("password", password_in);
    pref.putString("mqtt", mqtt_in);
    pref.putInt("port", port_in);
    pref.putInt("interval", interval_in);
    pref.putString("sensor", sensor_in);
    pref.putBool("config", true);
    pref.end();                         // Commit settings to flash
    pref.begin("twESP32Flora", false);  // Re-open for reading
  }
  String ssid = pref.getString("ssid", "");
  String password = pref.getString("password", "");
  String mqtt = pref.getString("mqtt", "");
  String sensor = pref.getString("sensor", "");
  int port = pref.getInt("port", 1883);
  int interval = pref.getInt("interval", 60);
  dry_soil = pref.getInt("dry_soil", 0);
  wet_soil = pref.getInt("wet_soil", 0);
  dry_rain = pref.getInt("dry_rain", 0);
  wet_rain = pref.getInt("wet_rain", 0);
  pref.end();
  Serial.printf("Config ssid=%s,mqtt=%s,port=%d sensor=%s interval=%d\n", ssid.c_str(), mqtt.c_str(), port, sensor.c_str(), interval);
  Serial.printf("dry_soil=%d wet_soil=%d dry_rain=%d wet_rain=%d\n", dry_soil,wet_soil,dry_rain,wet_rain);
  if (connectToWiFi(ssid, password)) {
    publishToMQTT(mqtt, port, sensor);
  }
  Serial.println("");
  Serial.println("Going to sleep now");
  esp_sleep_enable_timer_wakeup(interval * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
}

bool connectToWiFi(String ssid, String password) {
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");
  for (int i = 0; WiFi.status() != WL_CONNECTED; i++) {
    digitalWrite(LED, i % 2 == 0 ? HIGH : LOW);
    delay(1000);
    checkBootButton();
    Serial.print(".");
    if (i > 10) {
      Serial.println("\nFailed");
      return false;
    }
  }
  Serial.print("\nConnected, IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED, LOW);
  return true;
}

bool publishToMQTT(String mqtt, int port, String sensor) {
  if (sensor == "DHT22") {
    setDHT22Data();
  } else if (sensor == "BME280") {
    setBME280Data();
  }
  setSoilMoistureData();
  setRainSensorData();
  doc["client_mac"] = String(WiFi.macAddress());
  serializeJson(doc, outputtext, sizeof(outputtext));
  mqtt_client.setServer(mqtt.c_str(), port);
  mqtt_client.setKeepAlive(60);
  int i = 12;
  while (!mqtt_client.connected()) {
    String client_id = "twESP32Flora-" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT Broker as %s.....\n", client_id.c_str());
    if (mqtt_client.connect(client_id.c_str())) {
      Serial.println("Connected.");
      Serial.printf("Publishing message: %s\n", outputtext);
      // Publish the message.
      if (mqtt_client.publish((MQTT_TOPIC + String(WiFi.macAddress())).c_str(), outputtext)) {
        Serial.println("Publish ok");
        // Allow the client to process messages.
        mqtt_client.loop();
        // Disconnect from the broker.
        mqtt_client.disconnect();
        Serial.println("MQTT disconnected.");
        return true;
      } else {
        Serial.println("Publish failed");
        return false;
      }
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds.");
      delay(5000);
      i--;
      if (i < 0) {
        Serial.println("Retry over.");
        return false;
      }
    }
  }
  return false;
}

void setDHT22Data() {
  DHT_Unified dht(DHTPIN, DHTTYPE);
  dht.begin();
  delay(10);  // Wait for sensor stabilization
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (!isnan(event.temperature)) {
    doc["temperature"] = event.temperature;
  } else {
    Serial.println(F("Error reading temperature!"));
  }
  dht.humidity().getEvent(&event);
  if (!isnan(event.relative_humidity)) {
    doc["humidity"] = event.relative_humidity;
  } else {
    Serial.println(F("Error reading humidity!"));
  }
}

void setBME280Data() {
  // I2C communication instance
  Adafruit_BME280 bme;
  // Specify standard I2C pins for XIAO ESP32C3
  Wire.begin(SDA, SCL);
  // Initialize with 0x77
  if (!bme.begin(0x77, &Wire)) {
    // If that fails, try 0x76
    if (!bme.begin(0x76, &Wire)) {
      Serial.println(F("BME280 not found."));
      return;
    }
  }
  doc["temperature"] = bme.readTemperature();
  doc["pressure"] = (bme.readPressure() / 100.0F);
  doc["humidity"] = bme.readHumidity();
}


void setSoilMoistureData() {
  if (dry_soil == 0 && wet_soil == 0) {
    return;
  }
  doc["soil_moisture"] = getAnalog(SOIL_MOISTURE_SENSOR, dry_soil, wet_soil);
}

void setRainSensorData() {
  if (dry_rain == 0 && wet_rain == 0) {
    return;
  }
  doc["rain_intensity"] = getAnalog(RAIN_SENSOR_ANALOG, dry_rain, wet_rain);
  doc["rain"] = digitalRead(RAIN_SENSOR_DIGITAL) == LOW ? true : false;
}


// Input line form serial
String getInput(const char *msg, bool require) {
  String in = "";
  do {
    Serial.println(msg);
    while (Serial.available() < 1) {
      checkBootButton();
      delay(100);
    }
    in = Serial.readStringUntil('\n');
    in.trim();
    if (strlen(msg) > 0) {  // Only echo if there was a message to prompt.
      Serial.println(in.c_str());
    }
  } while (in == "" && require);
  if (in == "cancel") {
    pref.begin("twESP32Flora", false);
    pref.putBool("config", pref.getBool("config", false));  // Restore config
    pref.end();
    Serial.println("setup cancel and restart now");
    delay(1000);
    ESP.restart();
  }
  return in;
}

bool getYesNo(const char *msg) {
  String s;
  do {
    s = getInput(msg, true);
    s.toLowerCase();
  } while (s != "yes" && s != "y" && s != "no" && s != "n");
  return (s == "yes" || s == "y");
}


String getIPAddress() {
  String s;
  IPAddress ip;
  do {
    s = getInput("enter mqtt ip:");
  } while (!ip.fromString(s.c_str()));
  return s;
}

String getSensorType() {
  String s;
  do {
    s = getInput("enter sensor type (BME280 or DHT22):");
  } while (s != "BME280" && s != "DHT22");
  return s;
}

int getPortNumber(int def) {
  while (true) {
    String in = getInput("enter mqtt port (default 1883):", false);
    if (in == "") {
      break;
    }
    int p = atoi(in.c_str());
    if (p > 0 && p < 0xffff) {
      def = p;
      break;
    }
  }
  return def;
}

int getInterval(int def) {
  while (true) {
    String in = getInput("enter monitor interval sec (default 60):", false);
    if (in == "") {
      break;
    }
    int p = atoi(in.c_str());
    if (p > 0 && p < 3600 * 24) {
      def = p;
      break;
    }
  }
  return def;
}

void checkBootButton() {
  for (int i = 0; digitalRead(BOOT) == LOW; i++) {
    digitalWrite(LED, i % 2 == 0 ? HIGH : LOW);
    delay(500);
    Serial.print(".");
    if (i > 5) {  // about 5 seconds
      pref.begin("twESP32Flora", false);
      pref.clear();
      pref.end();
      Serial.printf("\nconfig clear and restart now\n");
      delay(1000);
      ESP.restart();
    }
  }
  digitalWrite(LED, LOW);
}

// getAnalogRaw
int getAnalogRaw(int io) {
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(io);
    delay(2);
  }
  return (sum / 10);
}

//  Get values from soil moisture and rain sensors as a percentage.
float getAnalog(int io, int dry, int wet) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(io);
    delay(2);
  }
  float rawValue = (float)sum / 10.0;
  // Calculate moisture percentage from ADC value
  // Using the map function to convert the calibration value range to 0-100%
  float percent = map(rawValue, (float)dry, (float)wet, 0.0, 100.0);

  // Clamp the value to the 0-100 range to prevent inversion of the calculation.
  percent = constrain(percent, 0.0, 100.0);

  return (percent);
}