#include "effects.h"
#include <FastLED.h>

CRGB leds[NUM_LEDS];

unsigned long lastSignalTime = 0;
unsigned long activeEffectStartedAt = 0;

bool crownActive = false;
bool signalWarning = false;
bool signalWarningEnabled = true;
bool activationSequenceActive = false;
bool syncSequenceActive = false;
bool groupStrobeSequenceActive = false;
bool effectFadeInActive = false;
bool darkHoldActive = false;
bool powerCheckActive = false;
bool blackoutOverrideActive = false;
uint8_t activeEffectId = EFFECT_DEBUG_HOPS;
uint8_t activeIntensity = 180;
uint8_t targetIntensity = 180;
uint8_t activeSpeed = 80;
uint8_t activeHopCount = 0;
uint32_t activePrimaryColor = 0xFFFFFF;
uint32_t activeSecondaryColor = 0x000000;
struct_message queuedActivationMessage = {};
struct_message queuedSyncMessage = {};
unsigned long syncSequenceStartedAt = 0;
unsigned long groupStrobeStartedAt = 0;
unsigned long effectFadeInStartedAt = 0;
unsigned long powerCheckStartedAt = 0;

CRGB colorFromHex(uint32_t color) {
    return CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

CRGB colorForHop(uint8_t hopCount) {
    if (hopCount == 0) {
        return CRGB::Blue;
    }
    if (hopCount == 1) {
        return CRGB::Green;
    }
    if (hopCount == 2) {
        return CRGB::Orange;
    }
    return CRGB::Purple;
}

void showSolid(CRGB color, uint8_t intensity) {
    color.nscale8_video(intensity);
    fill_solid(leds, NUM_LEDS, color);
}

uint8_t effectBpm(uint8_t fallback) {
    return activeSpeed == 0 ? fallback : activeSpeed;
}

CRGB scaledPrimary(uint8_t scale = 255) {
    CRGB color = colorFromHex(activePrimaryColor);
    color.nscale8_video(scale);
    return color;
}

CRGB scaledSecondary(uint8_t scale = 255) {
    CRGB color = colorFromHex(activeSecondaryColor);
    color.nscale8_video(scale);
    return color;
}

void effectsShowNoSignal() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[0] = CRGB(15, 0, 0);
    FastLED.show();
}

void effectsInit() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setMaxPowerInVoltsAndMilliamps(LED_POWER_VOLTAGE, LED_POWER_MILLIAMPS);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}

void effectsSetActive(bool active) {
    crownActive = active;
    signalWarning = false;
    activationSequenceActive = false;
    syncSequenceActive = false;
    groupStrobeSequenceActive = false;
    effectFadeInActive = false;
    darkHoldActive = false;
    powerCheckActive = false;
    lastSignalTime = millis();

    if (!crownActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
    }
}

void effectsSetSignalWarningEnabled(bool enabled) {
    signalWarningEnabled = enabled;
    if (!signalWarningEnabled) {
        signalWarning = false;
    }
}

void effectsNoteSignal() {
    lastSignalTime = millis();
    signalWarning = false;
}

void effectsStartPowerCheck() {
    crownActive = false;
    signalWarning = false;
    activationSequenceActive = false;
    syncSequenceActive = false;
    groupStrobeSequenceActive = false;
    effectFadeInActive = false;
    darkHoldActive = false;
    powerCheckActive = true;
    blackoutOverrideActive = false;
    powerCheckStartedAt = millis();
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[0] = CRGB::Green;
    FastLED.show();
}

void effectsSetBlackout(bool active) {
    blackoutOverrideActive = active;
    lastSignalTime = millis();

    if (blackoutOverrideActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
    }
}

void applyEffectMessage(const struct_message& message) {
    bool effectChanged =
        activeEffectId != message.effectId ||
        activePrimaryColor != message.primaryColor ||
        activeSecondaryColor != message.secondaryColor;

    activeEffectId = message.effectId;
    targetIntensity = message.intensity;
    activeIntensity = message.intensity;
    activeSpeed = message.speed;
    activeHopCount = message.hopCount;
    activePrimaryColor = message.primaryColor;
    activeSecondaryColor = message.secondaryColor;
    lastSignalTime = millis();
    signalWarning = false;
    blackoutOverrideActive = false;

    if (effectChanged) {
        activeEffectStartedAt = lastSignalTime;
    }
}

void startEffectFadeIn(const struct_message& message) {
    applyEffectMessage(message);
    targetIntensity = message.intensity;
    activeIntensity = 0;
    effectFadeInActive = true;
    darkHoldActive = false;
    effectFadeInStartedAt = millis();
}

void effectsStartActivation(const struct_message& message) {
    crownActive = true;
    signalWarning = false;
    activationSequenceActive = true;
    syncSequenceActive = false;
    groupStrobeSequenceActive = false;
    effectFadeInActive = false;
    darkHoldActive = false;
    powerCheckActive = false;
    blackoutOverrideActive = false;
    queuedActivationMessage = message;
    lastSignalTime = millis();
    activeEffectStartedAt = lastSignalTime;
}

void effectsStartGroupStrobe() {
    if (!crownActive) {
        return;
    }

    signalWarning = false;
    groupStrobeSequenceActive = true;
    darkHoldActive = false;
    powerCheckActive = false;
    lastSignalTime = millis();
    groupStrobeStartedAt = lastSignalTime;
}

void effectsStartSync(const struct_message& message) {
    crownActive = true;
    signalWarning = false;
    activationSequenceActive = false;
    syncSequenceActive = true;
    groupStrobeSequenceActive = false;
    effectFadeInActive = false;
    darkHoldActive = false;
    powerCheckActive = false;
    blackoutOverrideActive = false;
    queuedSyncMessage = message;
    lastSignalTime = millis();
    syncSequenceStartedAt = lastSignalTime;
}

void effectsApplyMessage(const struct_message& message) {
    if ((activationSequenceActive || syncSequenceActive || groupStrobeSequenceActive || effectFadeInActive || darkHoldActive) && message.command == COMMAND_HEARTBEAT) {
        lastSignalTime = millis();
        signalWarning = false;
        return;
    }

    activationSequenceActive = false;
    groupStrobeSequenceActive = false;
    syncSequenceActive = false;
    effectFadeInActive = false;
    darkHoldActive = false;
    powerCheckActive = false;
    applyEffectMessage(message);
}

void showTripleWhiteStrobe(unsigned long elapsed) {
    unsigned long phase = elapsed % 140;
    bool flashOn = phase < 45 && elapsed < 420;
    fill_solid(leds, NUM_LEDS, flashOn ? CRGB::White : CRGB::Black);
}

void showActivationSequence(unsigned long elapsed) {
    if (elapsed >= ACTIVATION_SPARKLE_DURATION) {
        showTripleWhiteStrobe(elapsed - ACTIVATION_SPARKLE_DURATION);
        return;
    }

    uint8_t progress = map(elapsed, 0, ACTIVATION_SPARKLE_DURATION - 1, 0, 255);
    uint8_t eased = ease8InOutQuad(progress);
    uint8_t sparkleChance = map(eased, 0, 255, 18, 235);
    uint8_t burstCount = map(eased, 0, 255, 1, 7);
    CRGB violet = CRGB(130, 35, 255);
    CRGB darkBlue = CRGB(10, 20, 140);
    CRGB lightBlue = CRGB(80, 190, 255);
    CRGB sparkleColor;

    if (progress < 86) {
        sparkleColor = blend(violet, darkBlue, progress * 3);
    } else if (progress < 172) {
        sparkleColor = blend(darkBlue, lightBlue, (progress - 86) * 3);
    } else {
        sparkleColor = blend(lightBlue, CRGB::White, (progress - 172) * 3);
    }

    fadeToBlackBy(leds, NUM_LEDS, 95);

    if (random8() < sparkleChance) {
        uint8_t flashes = random8(1, burstCount + 1);
        for (uint8_t i = 0; i < flashes; i++) {
            CRGB spark = sparkleColor;
            if (eased > 185 && random8() < 80) {
                spark = CRGB::White;
            }
            leds[random8(NUM_LEDS)] += spark;
        }
    }
}

void updateFadeIn(unsigned long now) {
    if (!effectFadeInActive) {
        return;
    }

    unsigned long elapsed = now - effectFadeInStartedAt;
    if (elapsed >= EFFECT_FADE_IN_DURATION) {
        activeIntensity = targetIntensity;
        effectFadeInActive = false;
        return;
    }

    uint8_t progress = map(elapsed, 0, EFFECT_FADE_IN_DURATION - 1, 0, 255);
    activeIntensity = scale8(targetIntensity, ease8InOutQuad(progress));
}

void effectsUpdate(unsigned long now) {
    if (blackoutOverrideActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        return;
    }

    if (powerCheckActive) {
        if (now - powerCheckStartedAt >= POWER_CHECK_DURATION) {
            powerCheckActive = false;
            fill_solid(leds, NUM_LEDS, CRGB::Black);
        } else {
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            leds[0] = CRGB::Green;
        }
        FastLED.show();
        return;
    }

    if (!crownActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        return;
    }

    signalWarning = signalWarningEnabled && now - lastSignalTime > SIGNAL_TIMEOUT;

    if (activationSequenceActive) {
        unsigned long elapsed = now - activeEffectStartedAt;
        if (elapsed >= ACTIVATION_SEQUENCE_DURATION) {
            activationSequenceActive = false;
            startEffectFadeIn(queuedActivationMessage);
        } else {
            showActivationSequence(elapsed);
            if (signalWarning) {
                leds[0] = CRGB(15, 0, 0);
            }
            FastLED.show();
            return;
        }
    }

    if (groupStrobeSequenceActive) {
        unsigned long elapsed = now - groupStrobeStartedAt;
        if (elapsed >= ACTIVATION_STROBE_DURATION) {
            groupStrobeSequenceActive = false;
            darkHoldActive = true;
        } else {
            showTripleWhiteStrobe(elapsed);
            if (signalWarning) {
                leds[0] = CRGB(15, 0, 0);
            }
            FastLED.show();
            return;
        }
    }

    if (syncSequenceActive) {
        unsigned long elapsed = now - syncSequenceStartedAt;
        if (elapsed >= SYNC_STROBE_DURATION) {
            syncSequenceActive = false;
            startEffectFadeIn(queuedSyncMessage);
        } else {
            showTripleWhiteStrobe(elapsed);
            if (signalWarning) {
                leds[0] = CRGB(15, 0, 0);
            }
            FastLED.show();
            return;
        }
    }

    if (darkHoldActive) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        if (signalWarning) {
            leds[0] = CRGB(15, 0, 0);
        }
        FastLED.show();
        return;
    }

    updateFadeIn(now);

    switch (activeEffectId) {
        case EFFECT_OFF:
            fill_solid(leds, NUM_LEDS, CRGB::Black);
            break;

        case EFFECT_SOLID:
            showSolid(colorFromHex(activePrimaryColor), activeIntensity);
            break;

        case EFFECT_BREATH: {
            uint8_t bpm = activeSpeed == 0 ? 1 : activeSpeed;
            uint8_t brightness = beatsin8(bpm, 10, activeIntensity);
            showSolid(colorFromHex(activePrimaryColor), brightness);
            break;
        }

        case EFFECT_CORONATION: {
            CRGB background = colorFromHex(activeSecondaryColor);
            background.nscale8_video(40);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t stepDelay = activeSpeed == 0 ? 80 : 255 - activeSpeed;
            if (stepDelay < 5) {
                stepDelay = 5;
            }
            uint8_t litCount = (((now - activeEffectStartedAt) / stepDelay) % (NUM_LEDS + 1));
            CRGB color = colorFromHex(activePrimaryColor);
            color.nscale8_video(activeIntensity);
            for (uint8_t i = 0; i < litCount; i++) {
                leds[i] = color;
            }
            break;
        }

        case EFFECT_SPARKLE: {
            CRGB background = colorFromHex(activeSecondaryColor);
            background.nscale8_video(20);
            fadeToBlackBy(leds, NUM_LEDS, 30);
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                leds[i] += background;
            }
            uint8_t chance = activeSpeed == 0 ? 20 : activeSpeed;
            if (random8() < chance) {
                CRGB color = colorFromHex(activePrimaryColor);
                color.nscale8_video(activeIntensity);
                leds[random8(NUM_LEDS)] += color;
            }
            break;
        }

        case EFFECT_WAVE: {
            CRGB background = colorFromHex(activeSecondaryColor);
            background.nscale8_video(35);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t bpm = effectBpm(1);
            uint8_t pos = beatsin8(bpm, 0, NUM_LEDS - 1);
            CRGB color = colorFromHex(activePrimaryColor);
            color.nscale8_video(activeIntensity);
            CRGB trail = color;
            trail.nscale8(120);
            leds[pos] = color;
            leds[(pos + NUM_LEDS - 1) % NUM_LEDS] += trail;
            leds[(pos + 1) % NUM_LEDS] += trail;
            break;
        }

        case EFFECT_AURORA: {
            uint16_t t = (now - activeEffectStartedAt) / 12;
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                uint8_t wave = inoise8(i * 38, t * effectBpm(35) / 32);
                leds[i] = blend(
                    scaledSecondary(activeIntensity / 2),
                    scaledPrimary(activeIntensity),
                    wave);
            }
            break;
        }

        case EFFECT_COMET_TWINS: {
            fadeToBlackBy(leds, NUM_LEDS, 55);
            uint8_t pos = beatsin8(effectBpm(45), 0, NUM_LEDS - 1);
            uint8_t opposite = (pos + NUM_LEDS / 2) % NUM_LEDS;
            CRGB primary = scaledPrimary(activeIntensity);
            CRGB secondary = scaledSecondary(activeIntensity);
            CRGB primaryTrail = primary;
            CRGB secondaryTrail = secondary;
            primaryTrail.nscale8(120);
            secondaryTrail.nscale8(120);
            leds[pos] += primary;
            leds[opposite] += secondary;
            leds[(pos + NUM_LEDS - 1) % NUM_LEDS] += primaryTrail;
            leds[(opposite + 1) % NUM_LEDS] += secondaryTrail;
            break;
        }

        case EFFECT_HEARTBEAT: {
            uint8_t beatA = beatsin8(effectBpm(52), 0, activeIntensity);
            uint8_t beatB = beatsin8(effectBpm(104), 0, activeIntensity / 2);
            showSolid(colorFromHex(activePrimaryColor), qadd8(beatA, beatB));
            break;
        }

        case EFFECT_COLOR_CHASE: {
            CRGB background = scaledSecondary(35);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t offset = ((now - activeEffectStartedAt) * effectBpm(75) / 700) % NUM_LEDS;
            CRGB primary = scaledPrimary(activeIntensity);
            CRGB trail = primary;
            trail.nscale8(90);
            for (uint8_t i = 0; i < NUM_LEDS; i += 4) {
                leds[(i + offset) % NUM_LEDS] = primary;
                leds[(i + offset + 1) % NUM_LEDS] += trail;
            }
            break;
        }

        case EFFECT_THEATER_CHASE: {
            CRGB background = scaledSecondary(25);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t stepDelay = 230 - effectBpm(90);
            if (stepDelay < 35) {
                stepDelay = 35;
            }
            uint8_t phase = ((now - activeEffectStartedAt) / stepDelay) % 3;
            CRGB primary = scaledPrimary(activeIntensity);
            for (uint8_t i = phase; i < NUM_LEDS; i += 3) {
                leds[i] = primary;
            }
            break;
        }

        case EFFECT_PORTAL: {
            CRGB background = scaledSecondary(25);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t radius = ((now - activeEffectStartedAt) * effectBpm(90) / 900) % (NUM_LEDS / 2);
            CRGB primary = scaledPrimary(activeIntensity);
            uint8_t left = (NUM_LEDS + (NUM_LEDS / 2) - radius) % NUM_LEDS;
            uint8_t right = ((NUM_LEDS / 2) + radius) % NUM_LEDS;
            leds[left] = primary;
            leds[right] = primary;
            leds[(left + 1) % NUM_LEDS] += scaledSecondary(activeIntensity / 2);
            leds[(right + NUM_LEDS - 1) % NUM_LEDS] += scaledSecondary(activeIntensity / 2);
            break;
        }

        case EFFECT_FIREWORKS: {
            fadeToBlackBy(leds, NUM_LEDS, 38);
            uint8_t center = ((now - activeEffectStartedAt) / 900) % NUM_LEDS;
            uint8_t stepDelay = 120 - effectBpm(70);
            if (stepDelay < 18) {
                stepDelay = 18;
            }
            uint8_t radius = ((now - activeEffectStartedAt) / stepDelay) % 8;
            CRGB primary = scaledPrimary(activeIntensity);
            CRGB secondary = scaledSecondary(activeIntensity);
            leds[(center + radius) % NUM_LEDS] += primary;
            leds[(center + NUM_LEDS - radius) % NUM_LEDS] += secondary;
            if (random8() < 30) {
                CRGB spark = primary;
                spark.nscale8(80);
                leds[random8(NUM_LEDS)] += spark;
            }
            break;
        }

        case EFFECT_GLITTER_RAIN: {
            fadeToBlackBy(leds, NUM_LEDS, 25);
            uint8_t drift = ((now - activeEffectStartedAt) * effectBpm(55) / 850) % NUM_LEDS;
            CRGB primary = scaledPrimary(activeIntensity);
            CRGB secondary = scaledSecondary(activeIntensity);
            if (random8() < 95) {
                leds[(random8(NUM_LEDS) + drift) % NUM_LEDS] += random8() & 1 ? primary : secondary;
            }
            break;
        }

        case EFFECT_LARSON_SCANNER: {
            fadeToBlackBy(leds, NUM_LEDS, 70);
            uint8_t pos = beatsin8(effectBpm(55), 0, NUM_LEDS - 1);
            leds[pos] += scaledPrimary(activeIntensity);
            break;
        }

        case EFFECT_PRISM: {
            uint8_t baseHue = ((now - activeEffectStartedAt) * effectBpm(28) / 900) & 0xFF;
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                leds[i] = CHSV(baseHue + i * 7, 220, activeIntensity);
            }
            break;
        }

        case EFFECT_RIPPLE: {
            fadeToBlackBy(leds, NUM_LEDS, 45);
            uint8_t center = ((now - activeEffectStartedAt) / 1800) % NUM_LEDS;
            uint8_t stepDelay = 150 - effectBpm(70);
            if (stepDelay < 22) {
                stepDelay = 22;
            }
            uint8_t radius = ((now - activeEffectStartedAt) / stepDelay) % (NUM_LEDS / 2);
            CRGB primary = scaledPrimary(activeIntensity);
            CRGB secondary = scaledSecondary(activeIntensity / 2);
            leds[(center + radius) % NUM_LEDS] += primary;
            leds[(center + NUM_LEDS - radius) % NUM_LEDS] += secondary;
            break;
        }

        case EFFECT_CONSTELLATION: {
            CRGB background = scaledSecondary(18);
            fill_solid(leds, NUM_LEDS, background);
            uint8_t t = (now - activeEffectStartedAt) / 24;
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                uint8_t sparkle = inoise8(i * 70, t);
                if (sparkle > 185) {
                    leds[i] += scaledPrimary(scale8(sparkle, activeIntensity));
                }
            }
            break;
        }

        case EFFECT_VOGUE_POSE: {
            CRGB background = scaledSecondary(30);
            fill_solid(leds, NUM_LEDS, background);
            uint16_t stepDelay = 820 - effectBpm(90) * 3;
            if (stepDelay < 120) {
                stepDelay = 120;
            }
            uint8_t pose = ((now - activeEffectStartedAt) / stepDelay) % 4;
            CRGB primary = scaledPrimary(activeIntensity);
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                if ((i + pose) % 8 == 0 || (i + pose) % 8 == 1) {
                    leds[i] = primary;
                }
            }
            break;
        }

        case EFFECT_STORM: {
            CRGB background = scaledSecondary(45);
            fill_solid(leds, NUM_LEDS, background);
            if (random8() < effectBpm(80)) {
                uint8_t pos = random8(NUM_LEDS);
                leds[pos] = scaledPrimary(activeIntensity);
                leds[(pos + 1) % NUM_LEDS] += scaledPrimary(activeIntensity / 2);
                leds[(pos + NUM_LEDS - 1) % NUM_LEDS] += scaledPrimary(activeIntensity / 2);
            }
            break;
        }

        case EFFECT_QUEEN_AURA: {
            uint8_t pulse = beatsin8(effectBpm(24), activeIntensity / 3, activeIntensity);
            for (uint8_t i = 0; i < NUM_LEDS; i++) {
                uint8_t blendAmount = sin8(i * 16 + (now - activeEffectStartedAt) / 18);
                leds[i] = blend(scaledSecondary(pulse), scaledPrimary(pulse), blendAmount);
            }
            if (random8() < 18) {
                leds[random8(NUM_LEDS)] += CRGB(80, 80, 80);
            }
            break;
        }

        case EFFECT_DEBUG_HOPS:
        default:
            showSolid(colorForHop(activeHopCount), 255);
            break;
    }

    if (signalWarning) {
        leds[0] = CRGB(15, 0, 0);
    }

    FastLED.show();
}
