#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include "config.h"

void effectsInit();
void effectsSetActive(bool active);
void effectsStartActivation(const struct_message& message);
void effectsStartGroupStrobe();
void effectsStartSync(const struct_message& message);
void effectsApplyMessage(const struct_message& message);
void effectsUpdate(unsigned long now);
void effectsShowNoSignal();

#endif
