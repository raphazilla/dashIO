#pragma once
#include <Arduino.h>

bool lora_init();
void lora_update();   // call from sensor task loop — handles RX, TX, config changes
