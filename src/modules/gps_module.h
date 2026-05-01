#pragma once

bool gps_init();
void gps_update();   // call frequently to drain UART buffer
