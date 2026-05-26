#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include <Arduino_NetworkConfigurator.h>
#include "configuratorAgents/agents/BLEAgent.h"
#include "configuratorAgents/agents/SerialAgent.h"

void onCloudManualFeedChange();
void onCloudPHTreatmentConfirmedChange();
void onCloudRefillHopperChange();
void onUserMorningHourChange();
void onUserEveningHourChange();

String cloudHeaderMain;
String cloudLabelActions;
String cloudLabelAnalytics;
String cloudLabelVitals;
String cloudLastFeedingStatus;
String cloudLastPHTreatment;
float cloudFoodLevel;
float cloudPH;
float cloudTemperature;
int cloudFeedingCount;
int cloudPHTreatmentCount;
int cloudTDS;
int cloudTurbidity;
int userEveningHour;
int userMorningHour;
bool cloudFoodLowAlert;
bool cloudIsWaterSafe;
bool cloudManualFeed;
bool cloudPHAlert;
bool cloudPHTreatmentConfirmed;
bool cloudRefillHopper;
bool cloudSystemOnline;
bool cloudWaterWarning;

KVStore kvStore;
BLEAgentClass BLEAgent;
SerialAgentClass SerialAgent;
WiFiConnectionHandler ArduinoIoTPreferredConnection; 
NetworkConfiguratorClass NetworkConfigurator(ArduinoIoTPreferredConnection);

void initProperties(){
  NetworkConfigurator.addAgent(BLEAgent);
  NetworkConfigurator.addAgent(SerialAgent);
  NetworkConfigurator.setStorage(kvStore);
  
  ArduinoCloud.setConfigurator(NetworkConfigurator);

  ArduinoCloud.addProperty(cloudHeaderMain, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudLabelActions, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudLabelAnalytics, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudLabelVitals, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudLastFeedingStatus, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudLastPHTreatment, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudFoodLevel, READ, ON_CHANGE, NULL, 5);
  ArduinoCloud.addProperty(cloudPH, READ, ON_CHANGE, NULL, 0.1);
  ArduinoCloud.addProperty(cloudTemperature, READ, ON_CHANGE, NULL, 0.5);
  ArduinoCloud.addProperty(cloudFeedingCount, READ, ON_CHANGE, NULL, 1);
  ArduinoCloud.addProperty(cloudPHTreatmentCount, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudTDS, READ, ON_CHANGE, NULL, 10);
  ArduinoCloud.addProperty(cloudTurbidity, READ, ON_CHANGE, NULL, 5);
  ArduinoCloud.addProperty(cloudFoodLowAlert, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudIsWaterSafe, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudPHAlert, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudSystemOnline, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(cloudWaterWarning, READ, ON_CHANGE, NULL);
  
  // Interactive Read/Write Variables
  ArduinoCloud.addProperty(cloudManualFeed, READWRITE, ON_CHANGE, onCloudManualFeedChange);
  ArduinoCloud.addProperty(cloudPHTreatmentConfirmed, READWRITE, ON_CHANGE, onCloudPHTreatmentConfirmedChange);
  ArduinoCloud.addProperty(cloudRefillHopper, READWRITE, ON_CHANGE, onCloudRefillHopperChange);
  ArduinoCloud.addProperty(userMorningHour, READWRITE, ON_CHANGE, onUserMorningHourChange);
  ArduinoCloud.addProperty(userEveningHour, READWRITE, ON_CHANGE, onUserEveningHourChange);
}