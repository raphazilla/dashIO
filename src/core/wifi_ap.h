#pragma once

void wifiAP_init();
void wifiAP_task(void* pvParameters);
void wifiAP_notifyLoRaRx();   // call from sensor task when LoRa packet arrives
int  wifiAP_clientCount();
