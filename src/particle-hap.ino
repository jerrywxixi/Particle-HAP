/*
 * Project particle-hap
 * Description:
 * Author:
 * Date:
 */

#include "homekit/HKServer.h"

#include "LightBulbAccessory.h"
#include "WindowsShutterAccessory.h"

HKServer *hkServer = NULL;

HAPAccessoryDescriptor *acc = new WindowsShutterAccessory();

// setup() runs once, when the device is first turned on.
void setup() {
	randomSeed(Time.now());//we need to somehow init random seed, so device identity will be unique
  Serial.begin();

  acc->initAccessorySet();

  //HKPersistor().resetAll();

  hkServer = new HKServer(acc->getDeviceType(),"Particle","523-12-643");

  hkServer->setup();
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  hkServer->handle();
  acc->handle();
}
