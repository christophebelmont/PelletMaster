/*
  Christophe Belmont
  Complete project details
   - Micronova interface to Home Assistant using MQTT
     
  Using hardware interface from 
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define MQTT_port   1883
#define hydro_mode  1
#define ext_therm_switch 1

// Wifi credentials
#define ssid "CasaParigi"
#define password "Elsa2011Andrea2017Clara2019"

// MQTT broker credentials (set to NULL if not required)
#define MQTT_username ""
#define MQTT_password ""

// Change the variable to your Raspberry Pi IP address, so it connects to your MQTT broker
#define MQTT_server "192.168.86.250"

#define MQTT_topic "Home/Micronova/"

// Hardware card configuration
#define SERIAL_MODE SWSERIAL_8N2 //8 data bits, parity none, 2 stop bits
#define RESET_PIN   D5
#define RX_PIN      D3
#define TX_PIN      D4
#define ENABLE_RX   D2
#define RELAY_1     D1

// Define loops timers
#define GETSTATUS_INTERVAL    2500

//Checksum: Code+Address+Value on hexadecimal calculator
const char stoveOn[4] = {0x80, 0x21, 0x01, 0xA2};
const char stoveOff[4] = {0x80, 0x21, 0x06, 0xA7};
const char forceOff[4] = {0x80, 0x21, 0x00, 0xA1};

#define stoveStateAddr 0x21
#define ambTempAddr 0x01
#define fumesTempAddr 0x3E
#define flamePowerAddr 0x34
#define waterTempAddr 0x03

uint8_t stoveState, /*tempSet, */fumesTemp, flamePower, waterTemp /*, waterSet*/;
float ambTemp, waterPres;
char stoveRxData[2]; //When the heater is sending data, it sends two bytes: a checksum and the value
//0 - OFF, 1 - Starting, 2 - Pellet loading, 3 - Ignition, 4 - Work, 5 - Brazier cleaning, 6 - Final cleaning, 7 - Standby, 8 - Pellet missing alarm, 9 - Ignition failure alarm, 10 - Alarms (to be investigated)

// Timers auxiliar variables
long now = millis();
long previousMillis = 0;

// Serial interface to receive and send commands from/to stove
SoftwareSerial StoveSerial;

// Initialize web server for web OTA
AsyncWebServer server(80);

// Initializes the espClient. You should change the espClient name if you have multiple ESPs running in your home automation system
WiFiClient espClient;
PubSubClient client(espClient);

// Define all micronova topics
static const char* MicronovaTopicStatus = MQTT_topic "Status";
static const char* MicronovaTopicAPI = MQTT_topic "API";

// Define device information for MQTT to Home Assistant
#define state_topic MQTT_topic "/state"
#define onoff_topic MQTT_topic "/onoff"
#define ambtemp_topic MQTT_topic "/ambtemp"
#define fumetemp_topic MQTT_topic "/fumetemp"
#define flame_topic MQTT_topic "/flamepower"
#define watertemp_topic MQTT_topic "/watertemp"
//#define waterset_topic mqtt_topic "/waterset"
#define waterpres_topic MQTT_topic "/waterpres"
#define ext_therm_topic MQTT_topic "/exttherm"
#define in_topic MQTT_topic "/intopic"
#define device_information "{\"manufacturer\": \"Christophe Belmont\",\"identifiers\": [\"7a396f39-80d2-493b-8e8e-31a70e700bc6\"],\"model\": \"Micronova Controller\",\"name\": \"Micronova Controller\",\"sw_version\": \"1.0.0.0\"}"
static const char* MicronovaTopicMeas1 = MQTT_topic "Meas1";
static const char* MicronovaTopicMeas2 = MQTT_topic "Meas2";

// Initialize Wifi connection
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected - ESP IP address: ");
  Serial.println(WiFi.localIP());
}

// This functions reconnects your ESP8266 to your MQTT broker
// Change the function below if you want to subscribe to more topics with your ESP8266 
void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      // Attempt to connect
      /*
        YOU MIGHT NEED TO CHANGE THIS LINE, IF YOU'RE HAVING PROBLEMS WITH MQTT MULTIPLE CONNECTIONS
        To change the ESP device ID, you will have to give a new name to the ESP8266.
        Here's how it looks:
          if (client.connect("ESP8266Client")) {
        You can do it like this:
          if (client.connect("ESP1_Office")) {
        Then, for the other ESP:
          if (client.connect("ESP2_Garage")) {
        That should solve your MQTT multiple connections problem
        GENERATE RANDOM CLIENT ID
      */
      String clientId = "ESPClient-";
      clientId += String(random(0xffff), HEX); //Random client ID
      if (client.connect(clientId.c_str(), MQTT_username, MQTT_password,MicronovaTopicStatus,1,true,"{\"Micronova Online\":0}")) {
        Serial.println("connected");
        client.setBufferSize(1024);
      } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }

    // Once connected to MQTT Create Home Assistant configuration topics
    if (client.connected())
    {
      Serial.println("Creating config topics for Home Assistant");

/*      String switch_topic = "homeassistant/switch/Micronova/Controller/config";
      String switch_payload = "{\"name\": \"Controller\", \"command_topic\": \"" in_topic "\", \"state_topic\": \"" onoff_topic "\", \"payload_on\": \"ON\", \"payload_off\": \"OFF\", \"state_on\": \"ON\", \"state_off\": \"OFF\", \"retain\":false, \"optimistic\": false, \"qos\": 0, \"icon\": \"mdi:fire\", \"unique_id\": \"d5668e9c-843c-4330-ae53-a5bd135a4412\", \"device\": " device_information "}";
      client.publish(switch_topic.c_str(), switch_payload.c_str(), true);
*/

      String temperature_sensor_topic = "homeassistant/sensor/Micronova/Temperature/config";
      String temperature_sensor_payload = "{\"name\": \"Temperature\", \"state_topic\": \"";
      temperature_sensor_payload.concat(MicronovaTopicMeas1);
      temperature_sensor_payload.concat("\", \"qos\": 0, \"device_class\": \"temperature\", \"state_class\": \"measurement\", \"unit_of_measurement\": \"°C\", \"icon\": \"mdi:thermometer\", \"unique_id\": \"9db3245e-6ace-4d14-ac07-844cd68d245c\",\"value_template\": \"{{ value_json.water_temperature }}\",\"device\": " device_information "}");
      temperature_sensor_payload.concat(MicronovaTopicMeas1);
      client.publish(temperature_sensor_topic.c_str(), temperature_sensor_payload.c_str(), true);
/*
      String fumes_temperature_sensor_topic = "homeassistant/sensor/Micronova/FumesTemperature/config";
      String fumes_temperature_sensor_payload = "{\"name\": \"Fumes Temperature\", \"state_topic\": \"" fumetemp_topic "\", \"qos\": 0, \"device_class\": \"temperature\", \"state_class\": \"measurement\", \"unit_of_measurement\": \"°C\", \"icon\": \"mdi:thermometer\", \"unique_id\": \"3c72e1cf-bc22-499e-9f85-7c7e960f9a95\",\"device\": " device_information "}";
      client.publish(fumes_temperature_sensor_topic.c_str(), fumes_temperature_sensor_payload.c_str(), true);

      String state_sensor_topic = "homeassistant/sensor/Micronova/State/config";
      String state_sensor_payload = "{\"name\": \"State\", \"state_topic\": \"" state_topic "\", \"qos\": 0, \"icon\": \"mdi:fire-alert\", \"unique_id\": \"62fe5080-7668-409b-8451-323364b42eff\",\"device\": " device_information "}";
      client.publish(state_sensor_topic.c_str(), state_sensor_payload.c_str(), true);

      String flame_power_sensor_topic = "homeassistant/sensor/Micronova/FlamePower/config";
      String flame_power_sensor_payload = "{\"name\": \"Flame Power\", \"state_topic\": \"" flame_topic "\", \"qos\": 0, \"unit_of_measurement\": \"%\", \"icon\": \"mdi:fire\", \"unique_id\": \"c3aecb86-66e2-4358-bccb-e3b620f3d28b\",\"device\": " device_information "}";
      client.publish(flame_power_sensor_topic.c_str(), flame_power_sensor_payload.c_str(), true);

      if (hydro_mode == 1)
      {
        String water_temperature_sensor_topic = "homeassistant/sensor/Micronova/WaterTemperature/config";
        String water_temperature_sensor_payload = "{\"name\": \"Water Temperature\", \"state_topic\": \"" watertemp_topic "\", \"qos\": 0, \"device_class\": \"temperature\", \"state_class\": \"measurement\", \"unit_of_measurement\": \"°C\", \"icon\": \"mdi:coolant-temperature\", \"unique_id\": \"7eb9a6b2-8e26-49f0-b75a-dd97f537d856\",\"device\": " device_information "}";
        client.publish(water_temperature_sensor_topic.c_str(), water_temperature_sensor_payload.c_str(), true);

        String water_pressure_sensor_topic = "homeassistant/sensor/Micronova/WaterPressure/config";
        String water_pressure_sensor_payload = "{\"name\": \"Water Pressure\", \"state_topic\": \"" waterpres_topic "\", \"qos\": 0, \"device_class\": \"pressure\", \"unit_of_measurement\": \"bar\", \"icon\": \"mdi:gauge\", \"unique_id\": \"2e272bb3-7b55-4867-bf1a-b8772d0ae90d\", \"device\": " device_information "}";
        client.publish(water_pressure_sensor_topic.c_str(), water_pressure_sensor_payload.c_str(), true);
      }

      if (ext_therm_switch == 1)
      {
        String water_temperature_sensor_topic = "homeassistant/sensor/Micronova/ExternalThermSwitch/config";
        String water_temperature_sensor_payload = "{\"name\": \"External Therm Switch\", \"state_topic\": \"" ext_therm_topic "\", \"qos\": 0, \"device_class\": \"switch\", \"unique_id\": \"7eda08da-4486-43db-9e6f-a26471da174a\",\"device\": " device_information "}";
        client.publish(water_temperature_sensor_topic.c_str(), water_temperature_sensor_payload.c_str(), true);
      }

*/
      Serial.println("HomeAssistant topics created.");
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived on topic: ");
    Serial.print(topic);
    Serial.print(". Message: ");
    String messageTemp;

    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
      messageTemp += (char)payload[i];
    }
    Serial.println();

    // Feel free to add more if statements to control more GPIOs with MQTT

    // If a message is received on the topic esp32/output, you check if the message is either "on" or "off". 
    // Changes the output state according to the message
    if (String(topic) == MicronovaTopicAPI) {
      Serial.print("Changing output to ");
      if(messageTemp == "on"){
        // Oncommand
      }
      else if(messageTemp == "off"){
        // OffCommand
      }
    }
}

void checkStoveReply() //Works only when request is RAM
{
    uint8_t rxCount = 0;
    stoveRxData[0] = 0x00;
    stoveRxData[1] = 0x00;
    while (StoveSerial.available()) //It has to be exactly 2 bytes, otherwise it's an error
    {
        stoveRxData[rxCount] = StoveSerial.read();
        rxCount++;
    }
    digitalWrite(ENABLE_RX, HIGH);
    if (rxCount == 2)
    {
        byte val = stoveRxData[1];
        byte checksum = stoveRxData[0];
        byte param = checksum - val;
        Serial.printf("Param=%01x value=%01x ", param, val);
        switch (param)
        {
        case stoveStateAddr:
            stoveState = val;
            switch (stoveState)
            {
            case 0:
                client.publish(state_topic, "Off", true);
                delay(1000);
                client.publish(onoff_topic, "OFF", true);
                break;
            case 1:
                client.publish(state_topic, "Starting", true);
                delay(1000);
                client.publish(onoff_topic, "ON", true);
                break;
            case 2:
                client.publish(state_topic, "Pellet loading", true);
                delay(1000);
                client.publish(onoff_topic, "ON", true);
                break;
            case 3:
                client.publish(state_topic, "Ignition", true);
                delay(1000);
                client.publish(onoff_topic, "ON", true);
                break;
            case 4:
                client.publish(state_topic, "Working", true);
                delay(1000);
                client.publish(onoff_topic, "ON", true);
                break;
            case 5:
                client.publish(state_topic, "Brazier cleaning", true);
                break;
            case 6:
                client.publish(state_topic, "Final cleaning", true);
                delay(1000);
                client.publish(onoff_topic, "OFF", true);
                break;
            case 7:
                client.publish(state_topic, "Standby", true);
                delay(1000);
                client.publish(onoff_topic, "OFF", true);
                break;
            case 8:
                client.publish(state_topic, "Pellet missing", true);
                break;
            case 9:
                client.publish(state_topic, "Ignition failure", true);
                delay(1000);
                client.publish(onoff_topic, "OFF", true);
                break;
            case 10:
                client.publish(state_topic, "Alarm", true);
                break;
            }
            Serial.printf("Stove %s\n", stoveState ? "ON" : "OFF");
            break;
        case ambTempAddr:
            ambTemp = (float)val / 2;
            client.publish(ambtemp_topic, String(ambTemp).c_str(), true);
            Serial.print("T. amb. ");
            Serial.println(ambTemp);
            break;
        /*case tempSetAddr:
            tempSet = val;
            client.publish(tempset_topic, String(tempSet).c_str(), true);
            Serial.printf("T. set %d\n", tempSet);
            break;*/
        case fumesTempAddr:
            fumesTemp = val;
            client.publish(fumetemp_topic, String(fumesTemp).c_str(), true);
            Serial.printf("T. fumes %d\n", fumesTemp);
            break;
        case flamePowerAddr:
            if (stoveState < 6)
            {
                if (stoveState > 0)
                {
                    flamePower = map(val, 0, 16, 10, 100);
                }
            }
            else
            {
                flamePower = 0;
            }
            client.publish(flame_topic, String(flamePower).c_str(), true);
            Serial.printf("Fire %d\n", flamePower);
            break;
        case waterTempAddr:
            waterTemp = val;
            client.publish(watertemp_topic, String(waterTemp).c_str(), true);
            Serial.printf("T. water %d\n", waterTemp);
            break;
        }
    }
}

void getStoveState() //Get detailed stove state
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(stoveStateAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getAmbTemp() //Get room temperature
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(ambTempAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getFumeTemp() //Get flue gas temperature
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(fumesTempAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getFlamePower() //Get the flame power (0, 1, 2, 3, 4, 5)
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(flamePowerAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getWaterTemp() //Get the temperature of the water (if you have an hydro heater)
{
    const byte readByte = 0x00;
    StoveSerial.write(readByte);
    delay(1);
    StoveSerial.write(waterTempAddr);
    digitalWrite(ENABLE_RX, LOW);
    delay(80);
    checkStoveReply();
}

void getStates() //Calls all the get…() functions
{
    getStoveState();
    delay(100);
    getAmbTemp();
    delay(100);
    getFumeTemp();
    delay(100);
    getFlamePower();
    if (hydro_mode == 1)
    {
        delay(100);
        getWaterTemp();
        delay(100);
        /*getWaterSet();
        delay(100);*/
    }
}

void setup()
{
    Serial.begin(115200);

    // If an external relay controls a thermostat dry contact
    if (ext_therm_switch == 1)
    {    
      pinMode(RELAY_1, OUTPUT);
    }

    // Define hardware RX pin and begin serial transmission
    pinMode(ENABLE_RX, OUTPUT);
    digitalWrite(ENABLE_RX, HIGH); //The led of the optocoupler is off
    StoveSerial.begin(1200, SERIAL_MODE, RX_PIN, TX_PIN, false, 256);

    // Establish communication
    setup_wifi();
    client.setServer(MQTT_server, MQTT_port);
    client.setBufferSize(400);
    client.setCallback(callback);
    client.subscribe(MicronovaTopicAPI);

    // Setup HTTP OTA
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/plain", "Micronova controller");
    });

    AsyncElegantOTA.begin(&server);    // Start ElegantOTA
    server.begin();
    Serial.println("HTTP OTA server started");

}

void loop()
{
    // MQTT Loop and reconnect handling
    if (!client.connected())
    {
        reconnect();
        client.subscribe(MicronovaTopicAPI);
    }
    client.loop();

    // Main Loop
    unsigned long currentMillis = millis();
    if (previousMillis > currentMillis) { previousMillis = 0; }
    if (currentMillis - previousMillis >= GETSTATUS_INTERVAL)
    {
        previousMillis = currentMillis;
        getStates();
        //publishStates();
    }
}