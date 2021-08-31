//Change Config File to Connect the MQTT Broker and WiFi
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "MQTT_Config.h"
#include "BLEDevice.h"

#define NUMBER_OF_STRING 6
#define MAX_STRING_SIZE 40

#define WIFI_RDY 0b0001
#define MQTT_CHG 0b0010

#define LED 13

//static SemaphoreHandle_t barrier;
//TaskHandle_t h_listener;
static EventGroupHandle_t hevt;

//WiFiClientSecure wifiClient;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
bool transmit_flag = false;

BLEClient*  pClient;
BLERemoteService* pRemoteService;

bool topic_flag = false; //0 = smartdesk / 1 = smartroom

typedef struct message {
  char device_uuid_val[37];
  char service_uuid_val[37];
  char char_uuid_val[37];
  char employee_id_val[37];
}message_t;

typedef struct event {
  char device_uuid_val[37];
  char service_uuid_val[37];
  char char_uuid_val[37];
  char event_status_val[37];
  char event_time_val[37];
}event_t;

// Create the struct
message_t msg;
event_t evt;

// MQTT Callback Data
static char messageMacAddress[18];
static char deviceMacAddress[18];

// The remote service we wish to connect to.
//0000180a-0000-1000-8000-00805f9b34fb
//e54b0001-67f5-479e-8711-b3b99198ce6c
//A9A5941D-1681-14E8-E243-78685AB7D125
//E54B0001-67F5-479E-8711-B3B99198CE6C
//e0:5a:5a:c8:36:ac
//3c:71:bf:f5:5d:58

BLEUUID device_uuid("");
BLEUUID service_uuid("");
BLEUUID char_uuid("");

bool doConnect = false;
bool connected = false;
BLERemoteCharacteristic* pRemoteCharacteristic;
BLEAdvertisedDevice* myDevice;

void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.println((char*)pData);
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
    }

    void onDisconnect(BLEClient* pclient) {
      connected = false;
      Serial.println("onDisconnect");
    }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  
  //Serial.println(" - Created client");
  pClient  = BLEDevice::createClient();

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  pRemoteService = pClient->getService(service_uuid);
  //BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(service_uuid.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(char_uuid);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(char_uuid.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  return true;
}

/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());
      //Serial.print("Was looking for uuid:");
      //Serial.print(device_uuid);

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(device_uuid)) {
        Serial.print("===Found ESL with matching device UUID==");
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        Serial.print("doConnect = true");
      } // Found our server
    } // onResult
}; // MyAdvertisedDeviceCallbacks

void reconnectToTheBroker() {
  int numberOfConnectionsTried = 0;
  while (!mqttClient.connected()) {
    Serial.println("Reconnecting to MQTT Broker..");
    led_off(LED);
    if (mqttClient.connect(CLIENT_ID, MQTT_USER_NAME, MQTT_PASSWORD)) {
      Serial.println("MQTT Broker Connected.");
      led_on(LED);
      //subscribe to topic
      mqttClient.subscribe("/name");
      mqttClient.subscribe("/next-event");
    }
    else {
      //MQTT Could not reconnect, wifi/esp32 error
      Serial.print("Connection failed, rc=");
      Serial.print(mqttClient.state());
      numberOfConnectionsTried++;
      if (numberOfConnectionsTried > 5) {
        Serial.print("Rebooting the device...");
        ESP.restart();
      }
    }
    delay(500);
  }
}

void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi is diconnected.");
    delay(500);
  }
  Serial.println("Connected to the WiFi.");
  xEventGroupSetBits(hevt, WIFI_RDY);
}


void callback(char* topic, byte* payload, unsigned int length) {

  Serial.print("Callback - ");
  Serial.print("Message:");

  char messageTemp[MAX_STRING_SIZE * NUMBER_OF_STRING];
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    messageTemp[i] = payload[i];
  }
  messageTemp[length] = '\0';
  Serial.print("\n");

  const char *delimeter = ";";

  /* -- Getting MacAddresses from both Device and Message -- */
  strcpy(messageMacAddress, strtok(messageTemp, delimeter));
  String getMAC = WiFi.macAddress();
  strcpy(deviceMacAddress, getMAC.c_str());

  Serial.print("deviceMacAddress: ");
  Serial.println(deviceMacAddress);

  if (strcmp(messageMacAddress, deviceMacAddress) == 0) {
    //smart desk
    if (strcmp(topic, "/name") == 0) {
      topic_flag = false;
      Serial.println("Detected message at the topic name");
      strcpy(msg.device_uuid_val, strtok(NULL, delimeter));
      strcpy(msg.service_uuid_val, msg.device_uuid_val);
      msg.service_uuid_val[7]='1';
      strcpy(msg.char_uuid_val, msg.device_uuid_val);
      msg.char_uuid_val[7]='2';
      strcpy(msg.employee_id_val, strtok(NULL, delimeter));

      Serial.print("Device uuid: "); Serial.println(msg.device_uuid_val);
      Serial.print("Service uuid: "); Serial.println(msg.service_uuid_val);
      Serial.print("Characteristic uuid: "); Serial.println(msg.char_uuid_val);
      Serial.print("Employee ID: "); Serial.println(msg.employee_id_val);
      

      // TODO
      // set EventGroupBits
      xEventGroupSetBits(hevt, MQTT_CHG);

      // Transmit the message data to queue.
      // transmit_flag = true;
    }
    //room reservation
    else if(strcmp(topic, "/next-event") == 0) {
      topic_flag = true;
      Serial.println("Detected message at the topic name");
      strcpy(evt.device_uuid_val, strtok(NULL, delimeter));
      strcpy(evt.service_uuid_val, evt.device_uuid_val);
      evt.service_uuid_val[7]='1';
      strcpy(evt.char_uuid_val, evt.device_uuid_val);
      evt.char_uuid_val[7]='2';
      strcpy(evt.event_status_val, strtok(NULL, delimeter));
      strcpy(evt.event_time_val, strtok(NULL, delimeter));

      Serial.print("Device uuid: "); Serial.println(evt.device_uuid_val);
      Serial.print("Service uuid: "); Serial.println(evt.service_uuid_val);
      Serial.print("Characteristic uuid: "); Serial.println(evt.char_uuid_val);
      Serial.print("Event Status: "); Serial.println(evt.event_status_val);
      Serial.print("Event Time: "); Serial.println(evt.event_time_val);

      // TODO
      // set EventGroupBits
      xEventGroupSetBits(hevt, MQTT_CHG);

      // Transmit the message data to queue.
      //transmit_flag = true;
    }
  }
  else {
    Serial.println("Different device");
  }
}

void led_on(int led)
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
}

void led_off(int led)
{
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
}

void setupMQTT() {
  //wifiClient.setCACert(ca_cert);
  mqttClient.setServer(MQTT_SERVER_NAME, MQTT_PORT);
  // set the callback function
  mqttClient.setCallback(callback);
  mqttClient.setKeepAlive(60);
}

// TODO: Create a mqtt_task 
// Connect to broker.
// Take the data package.
// Parse it.
// Add to the queue struct.
// Signal to the BLE Task.
static void mqtt_task(void *argp)
{
      xEventGroupWaitBits(
      hevt,            // Event group
      WIFI_RDY,        // bits to wait for
      pdFALSE,         // no clear
      pdFALSE,         // wait for all bits
      portMAX_DELAY);  // timeout

  BaseType_t rc;
  for(;;)
  { 
    // Connect to broker
    if (!mqttClient.connected()) {
      Serial.println("Reconnecting to the broker..");
      reconnectToTheBroker();
    }
    // Take the data package, parse it.
    mqttClient.loop();
    
    // if(transmit_flag == true)
    // {
    //   transmit_flag = false;

    //   Serial.println("Giving the semaphore..");
    //   // Signal to ble_task
    //   rc = xSemaphoreGive(barrier);
    //   // assert(rc == pdPASS); 
    //   Serial.println("Listener task is suspending..."); 
    //   vTaskSuspend(nullptr);     
    // }
  }
}

// TODO: Create a ble task 

// Take the queue values.
// Scan the ESL devices. 
// Connect to ESL according to the UUID value in the Queue.
// Send the data package. (If the data is bigger than 20 bytes then send it respectively.)
// Disconnect from device.
// Signal to the listener task.
static void ble_task(void *argp)
{

  BaseType_t s;
  BaseType_t rc;

  for(;;)
  {    
    // TODO
    // wait EventGroupBits
    xEventGroupWaitBits(
      hevt,            // Event group
      MQTT_CHG,        // bits to wait for
      pdTRUE,         // no clear
      pdFALSE,         // wait for all bits
      portMAX_DELAY);  // timeour

    // rc = xSemaphoreTake(barrier, portMAX_DELAY);
    // assert(rc == pdPASS);
    // Serial.println("Taking the semaphore..");
    
    // Transmit to BLEUUID
    if(topic_flag) {
      device_uuid = BLEUUID(evt.device_uuid_val);
      service_uuid = BLEUUID(evt.service_uuid_val);
      char_uuid = BLEUUID(evt.char_uuid_val);
    }
    else {
      device_uuid = BLEUUID(msg.device_uuid_val);
      service_uuid = BLEUUID(msg.service_uuid_val);
      char_uuid = BLEUUID(msg.char_uuid_val);
    }


    // Scan the BLE devices.
    BLEScan* pBLEScan = BLEDevice::getScan();

    // Connect to ESL according to the UUID value in the Queue.
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(15, false);

    if (doConnect == true) {
      if (connectToServer()) {
        Serial.println("We are now connected to the BLE Server.");
      } else {
        Serial.println("We have failed to connect to the server; there is nothin more we will do.");
      }
      
      doConnect = false;
    }
    else {
      Serial.println("doConnect = false");
      Serial.println(mqttClient.publish("/warning", "{ \"mac\": \"3c:71:bf:f5:5d:58\",\"warningExp\": \"ShelfLabel - UUID not found\", \"warningCode\": 2 }"));
    }

    if (connected) {
      String newString = "";
      
      if(topic_flag) {
        char* temp;
        temp = strcat(evt.event_status_val, "^");
        newString = strcat(temp, evt.event_time_val);
        Serial.println("Setting new characteristic value..");
        Serial.println(newString);
      }
      else {
        if(sizeof(msg.employee_id_val)>20) {
          char temp[20] = {0};
          strncpy(temp, msg.employee_id_val,20);
          temp[19]='.';
          newString = temp;
          newString.replace(" ","^");
        } else {
          newString = msg.employee_id_val;
          newString.replace(" ","^");
        }
        Serial.println("Setting new characteristic value..");
        Serial.println(newString);
      }
      
      // Set the characteristic's value to be the array of bytes that is actually a string.
      pRemoteCharacteristic->writeValue(newString.c_str(), newString.length());
      pClient->disconnect();
    }
    // Serial.println("Listener task is resuming..."); 
    // vTaskResume(h_listener);
  }
}

void setup() 
{
  int app_cpu = xPortGetCoreID();
  BaseType_t rc;

  // barrier = xSemaphoreCreateBinary();
  // assert(barrier);

  hevt = xEventGroupCreate();
  assert(hevt);

  Serial.begin(115200);

  Serial.println("Starting ESP32 Gateway application...");

  delay(2000);  // Allow USB to connect

  rc = xTaskCreatePinnedToCore(
    mqtt_task,
    "mqtttask",
    5000,       // Stack Size
    nullptr,
    1,           // Priortiy
    nullptr,     // Task Handle
    app_cpu      // CPU
  );
  assert(rc == pdPASS);
  //assert(h_listener);

  rc = xTaskCreatePinnedToCore(
    ble_task,
    "bletask",
    5000,       // Stack Size
    nullptr,
    1,           // Priortiy
    nullptr,     // Task Handle
    app_cpu      // CPU
  );
  assert(rc == pdPASS);

  connectToWiFi();
  setupMQTT();
  BLEDevice::init("");
}

void loop() 
{
  // Delete loop task.
  vTaskDelete(nullptr);
}
