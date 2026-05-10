#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include "config.h"

void effectsInit();
void effectsApplyMessage(const struct_message& message);
void effectsUpdate(unsigned long now);
void effectsShowNoSignal();

#endif
