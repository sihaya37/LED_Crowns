#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config.h"

#define COSTUME_LED_PIN 2
#define COSTUME_NUM_LEDS 500

// Prototype power limit: strip powered directly from a 5V/3A powerbank.
#define COSTUME_POWER_VOLTAGE 5
#define COSTUME_POWER_MILLIAMPS 2400
#define COSTUME_BRIGHTNESS 255

#define SEGMENT_COUNT 7
#define HEART_SECTION_LEN 10
#define LIGHTNING_MAX_BOLTS 8
#define LIGHTNING_MIN_GAP_MS 180UL
#define LIGHTNING_MAX_GAP_MS 980UL
#define LIGHTNING_MIN_TRAVEL_MS 420UL
#define LIGHTNING_MAX_TRAVEL_MS 1250UL

#define RED_EXPAND_MS 1000UL
#define RED_RELEASE_MS 1000UL
#define COSTUME_SPARKLE_OUT_MS 5000UL
#define COSTUME_SYNC_HOLD_MS 3000UL
#define COSTUME_SYNC_FADE_OUT_MS 500UL
#define COSTUME_SYNC_FADE_IN_MS 500UL

CRGB leds[COSTUME_NUM_LEDS];

struct Segment {
    uint16_t start;
    uint16_t length;
};

const Segment segments[SEGMENT_COUNT] = {
    {0, 74},
    {74, 54},
    {128, 67},
    {195, 28},
    {223, 26},
    {249, 28},
    {277, 60}
};

enum CostumeMode : uint8_t {
    MODE_IDLE,
    MODE_RED_EXPAND,
    MODE_RED_RELEASE,
    MODE_SPARKLE_OUT,
    MODE_DARK_WAIT,
    MODE_SYNC_STROBE,
    MODE_SYNC_HOLD,
    MODE_SYNC_FADE_OUT,
    MODE_SYNC_FADE_IN
};

struct LightningBolt {
    bool active;
    unsigned long startedAt;
    unsigned long duration;
    int startPos;
    int targetPos;
};

CostumeMode costumeMode = MODE_IDLE;
LightningBolt bolts[LIGHTNING_MAX_BOLTS] = {};
unsigned long modeStartedAt = 0;
unsigned long nextBoltAt = 0;
unsigned long pendingSyncAt = 0;
bool pendingSync = false;
struct_message currentShowMessage = {};

uint16_t segmentEnd(uint8_t segmentIndex) {
    return segments[segmentIndex].start + segments[segmentIndex].length - 1;
}

uint8_t easeInCubic8(uint8_t value) {
    uint16_t squared = ((uint16_t)value * value) / 255;
    return (uint8_t)((squared * value) / 255);
}

uint8_t heartbeatLevel(unsigned long now) {
    unsigned long phase = now % 1500UL;

    if (phase < 90) {
        return map(phase, 0, 89, 0, 255);
    }
    if (phase < 240) {
        return map(phase, 90, 239, 255, 0);
    }
    if (phase < 350) {
        return map(phase, 240, 349, 0, 255);
    }
    if (phase < 590) {
        return map(phase, 350, 589, 255, 0);
    }

    return 0;
}

void addPixel(int pos, const CRGB& color, uint8_t scale) {
    if (pos < 0 || pos >= COSTUME_NUM_LEDS) {
        return;
    }

    CRGB scaled = color;
    scaled.nscale8_video(scale);
    leds[pos] += scaled;
}

bool isHeartPixel(int pos) {
    if (pos < 0 || pos >= COSTUME_NUM_LEDS) {
        return false;
    }

    bool segment1 = pos >= segments[0].start && pos < segments[0].start + HEART_SECTION_LEN;
    bool segment2 = pos <= segmentEnd(1) && pos > segmentEnd(1) - HEART_SECTION_LEN;
    bool segment3Start = pos >= segments[2].start && pos < segments[2].start + HEART_SECTION_LEN;
    bool segment3End = pos <= segmentEnd(2) && pos > segmentEnd(2) - HEART_SECTION_LEN;
    bool segment7 = pos >= segments[6].start && pos < segments[6].start + HEART_SECTION_LEN;
    return segment1 || segment2 || segment3Start || segment3End || segment7;
}

void addLightningPixel(int pos, const CRGB& color, uint8_t scale) {
    if (isHeartPixel(pos)) {
        return;
    }

    addPixel(pos, color, scale);
}

void drawRedSection(int start, int direction, uint8_t pulseScale) {
    const uint8_t basePct[HEART_SECTION_LEN] = {18, 14, 11, 8, 6, 5, 4, 3, 2, 1};
    const uint8_t beatPct[HEART_SECTION_LEN] = {100, 92, 82, 70, 58, 46, 34, 24, 16, 9};

    for (uint8_t i = 0; i < HEART_SECTION_LEN; i++) {
        int pos = start + direction * i;
        if (pos < 0 || pos >= COSTUME_NUM_LEDS) {
            continue;
        }

        uint8_t percent = map(pulseScale, 0, 255, basePct[i], beatPct[i]);
        CRGB red = CRGB::Red;
        red.nscale8_video((uint16_t)percent * 255 / 100);
        leds[pos] += red;
    }
}

void drawIdleHeart(unsigned long now, uint8_t globalScale = 255) {
    uint8_t pulse = scale8(heartbeatLevel(now), globalScale);

    // Segment 1: first 10 pixels from the right shoulder blade.
    drawRedSection(segments[0].start, 1, pulse);

    // Segment 2: last 10 pixels toward the right shoulder blade.
    drawRedSection(segmentEnd(1), -1, pulse);

    // Segment 3: first and last 10 pixels, shoulder blade and chest sides.
    drawRedSection(segments[2].start, 1, pulse);
    drawRedSection(segmentEnd(2), -1, pulse);

    // Segment 7: first 10 pixels from the chest toward the right wrist.
    drawRedSection(segments[6].start, 1, pulse);
}

void scheduleNextBolt(unsigned long now) {
    nextBoltAt = now + random16(LIGHTNING_MIN_GAP_MS, LIGHTNING_MAX_GAP_MS);
}

void startBolt(unsigned long now, int startPos, int targetPos) {
    for (uint8_t i = 0; i < LIGHTNING_MAX_BOLTS; i++) {
        if (bolts[i].active) {
            continue;
        }

        bolts[i].active = true;
        bolts[i].startedAt = now;
        bolts[i].duration = random16(LIGHTNING_MIN_TRAVEL_MS, LIGHTNING_MAX_TRAVEL_MS);
        bolts[i].startPos = startPos;
        bolts[i].targetPos = targetPos;
        return;
    }
}

void startRandomBolt(unsigned long now) {
    uint8_t route = random8(9);

    switch (route) {
        case 0:
            startBolt(now, segmentEnd(0), segments[0].start + HEART_SECTION_LEN);
            break;
        case 1:
            startBolt(now, segments[1].start, segmentEnd(1) - HEART_SECTION_LEN);
            break;
        case 2: {
            uint16_t middle = segments[2].start + segments[2].length / 2;
            startBolt(now, middle, segments[2].start + HEART_SECTION_LEN);
            break;
        }
        case 3: {
            uint16_t middle = segments[2].start + segments[2].length / 2;
            startBolt(now, middle, segmentEnd(2) - HEART_SECTION_LEN);
            break;
        }
        case 4:
            startBolt(now, segmentEnd(6), segments[6].start + HEART_SECTION_LEN);
            break;
        default: {
            uint8_t segmentIndex = 3 + random8(3);
            bool forward = random8() & 1;
            startBolt(
                now,
                forward ? segments[segmentIndex].start : segmentEnd(segmentIndex),
                forward ? segmentEnd(segmentIndex) : segments[segmentIndex].start);
            break;
        }
    }
}

void drawLightningBolt(const LightningBolt& bolt, unsigned long now) {
    unsigned long elapsed = now - bolt.startedAt;
    uint8_t progress = elapsed >= bolt.duration
        ? 255
        : map(elapsed, 0, bolt.duration - 1, 0, 255);
    uint8_t eased = easeInCubic8(progress);
    int pos = bolt.startPos + ((int32_t)(bolt.targetPos - bolt.startPos) * eased) / 255;
    int direction = bolt.targetPos >= bolt.startPos ? -1 : 1;

    CRGB boltColor = CRGB(175, 225, 255);
    for (uint8_t i = 0; i < 8; i++) {
        addLightningPixel(pos + direction * i, boltColor, 255 - i * 26);
    }

    addLightningPixel(pos, CRGB::White, 255);
    if (random8() < 85) {
        addLightningPixel(pos + direction * random8(3, 12), CRGB::White, random8(70, 180));
    }
}

void drawIdleLightning(unsigned long now) {
    if (nextBoltAt == 0) {
        scheduleNextBolt(now);
    }

    if ((long)(now - nextBoltAt) >= 0) {
        startRandomBolt(now);
        scheduleNextBolt(now);
    }

    for (uint8_t i = 0; i < LIGHTNING_MAX_BOLTS; i++) {
        if (!bolts[i].active) {
            continue;
        }

        if (now - bolts[i].startedAt >= bolts[i].duration) {
            bolts[i].active = false;
            continue;
        }

        drawLightningBolt(bolts[i], now);
    }
}

void drawExpandingRedOnSegment(uint8_t segmentIndex, uint8_t progress) {
    uint16_t start = segments[segmentIndex].start;
    uint16_t end = segmentEnd(segmentIndex);
    uint16_t length = segments[segmentIndex].length;
    uint16_t visible = ((uint32_t)length * progress) / 255;

    for (uint16_t i = 0; i < visible; i++) {
        leds[start + i] += CRGB(255, 0, 18);
        leds[end - i] += CRGB(255, 0, 18);
    }
}

void drawRedExpand(unsigned long elapsed) {
    uint8_t progress = elapsed >= RED_EXPAND_MS
        ? 255
        : ease8InOutQuad(map(elapsed, 0, RED_EXPAND_MS - 1, 0, 255));

    drawExpandingRedOnSegment(0, progress);
    drawExpandingRedOnSegment(1, progress);
    drawExpandingRedOnSegment(2, progress);
    drawExpandingRedOnSegment(6, progress);
}

void drawRedRange(uint16_t start, uint16_t end) {
    for (uint16_t pos = start; pos <= end && pos < COSTUME_NUM_LEDS; pos++) {
        leds[pos] += CRGB(255, 0, 18);
    }
}

void drawRedRelease(unsigned long elapsed) {
    uint8_t progress = elapsed >= RED_RELEASE_MS
        ? 255
        : ease8InOutQuad(map(elapsed, 0, RED_RELEASE_MS - 1, 0, 255));

    uint16_t clear0 = ((uint32_t)segments[0].length * progress) / 255;
    uint16_t clear1 = ((uint32_t)segments[1].length * progress) / 255;
    uint16_t clear3 = ((uint32_t)(segments[2].length / 2) * progress) / 255;
    uint16_t clear7 = ((uint32_t)segments[6].length * progress) / 255;

    if (clear0 < segments[0].length) {
        drawRedRange(segments[0].start + clear0, segmentEnd(0));
    }
    if (clear1 < segments[1].length) {
        drawRedRange(segments[1].start, segmentEnd(1) - clear1);
    }

    uint16_t seg3Start = segments[2].start;
    uint16_t seg3End = segmentEnd(2);
    uint16_t seg3MiddleLeft = segments[2].start + segments[2].length / 2 - 1;
    uint16_t seg3MiddleRight = seg3MiddleLeft + 1;

    if (clear3 < segments[2].length / 2) {
        drawRedRange(seg3Start + clear3, seg3MiddleLeft);
        drawRedRange(seg3MiddleRight, seg3End - clear3);
    }

    if (clear7 < segments[6].length) {
        drawRedRange(segments[6].start + clear7, segmentEnd(6));
    }
}

CRGB sparkleColorForProgress(uint8_t progress) {
    CRGB violet = CRGB(130, 35, 255);
    CRGB darkBlue = CRGB(10, 20, 140);
    CRGB lightBlue = CRGB(80, 190, 255);

    if (progress < 86) {
        return blend(violet, darkBlue, progress * 3);
    }
    if (progress < 172) {
        return blend(darkBlue, lightBlue, (progress - 86) * 3);
    }
    return blend(lightBlue, CRGB::White, (progress - 172) * 3);
}

void sparkleAlongPath(int startPos, int endPos, uint8_t progress, uint8_t chance) {
    int front = startPos + ((int32_t)(endPos - startPos) * progress) / 255;
    int direction = endPos >= startPos ? 1 : -1;
    CRGB color = sparkleColorForProgress(progress);

    for (uint8_t i = 0; i < 10; i++) {
        if (random8() > chance) {
            continue;
        }

        int offset = random8(0, 18);
        int pos = front - direction * offset;
        addPixel(pos, color, random8(90, 255));
    }
}

void drawSparkleOut(unsigned long elapsed) {
    uint8_t progress = elapsed >= COSTUME_SPARKLE_OUT_MS
        ? 255
        : ease8InOutQuad(map(elapsed, 0, COSTUME_SPARKLE_OUT_MS - 1, 0, 255));
    uint8_t chance = map(progress, 0, 255, 18, 235);
    uint8_t burstCount = map(progress, 0, 255, 1, 7);
    uint16_t activeLedCount = segments[SEGMENT_COUNT - 1].start + segments[SEGMENT_COUNT - 1].length;
    CRGB color = sparkleColorForProgress(progress);

    if (random8() < chance) {
        uint8_t flashes = random8(1, burstCount + 1);
        for (uint8_t i = 0; i < flashes; i++) {
            uint16_t pos = random16(activeLedCount);
            CRGB spark = color;
            if (progress > 185 && random8() < 80) {
                spark = CRGB::White;
            }
            leds[pos] += spark;
        }
    }
}

void showTripleWhiteStrobe(unsigned long elapsed) {
    unsigned long phase = elapsed % 140;
    bool flashOn = phase < 45 && elapsed < 420;
    fill_solid(leds, COSTUME_NUM_LEDS, flashOn ? CRGB::White : CRGB::Black);
}

CRGB colorFromHex(uint32_t color) {
    return CRGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

void drawSharedEffect(unsigned long now, uint8_t globalScale) {
    CRGB primary = colorFromHex(currentShowMessage.primaryColor);
    CRGB secondary = colorFromHex(currentShowMessage.secondaryColor);
    if (secondary == CRGB::Black) {
        secondary = CRGB(12, 0, 28);
    }

    uint8_t baseHue = ((now - modeStartedAt) * max<uint8_t>(currentShowMessage.speed, 24) / 900) & 0xFF;

    for (uint8_t s = 0; s < SEGMENT_COUNT; s++) {
        for (uint16_t offset = 0; offset < segments[s].length; offset++) {
            uint16_t pos = segments[s].start + offset;
            uint8_t wave = sin8(offset * 10 + baseHue);
            CRGB color = blend(secondary, primary, wave);

            if (currentShowMessage.effectId == EFFECT_PRISM) {
                color = CHSV(baseHue + offset * 4, 220, 255);
            } else if (currentShowMessage.effectId == EFFECT_STORM && random8() < 24) {
                color = CRGB::White;
            } else if (currentShowMessage.effectId == EFFECT_SPARKLE && random8() < 42) {
                color = CRGB::White;
            }

            color.nscale8_video(scale8(currentShowMessage.intensity, globalScale));
            leds[pos] += color;
        }
    }
}

void handleActivationCommand(const struct_message& message) {
    for (uint8_t i = 0; i < LIGHTNING_MAX_BOLTS; i++) {
        bolts[i].active = false;
    }

    currentShowMessage = message;
    costumeMode = MODE_RED_EXPAND;
    modeStartedAt = millis();
    pendingSync = true;
    pendingSyncAt = modeStartedAt + YARA_TO_CROWN_ACTIVATION_DELAY + CROWN_SYNC_DELAY;
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(struct_message)) {
        return;
    }

    struct_message incoming;
    memcpy(&incoming, data, sizeof(incoming));

    if (incoming.protocolVersion != PROTOCOL_VERSION) {
        return;
    }

    if (incoming.command == COMMAND_ACTIVATE_CROWN) {
        handleActivationCommand(incoming);
    }
}

void renderIdle(unsigned long now, uint8_t scale = 255) {
    fadeToBlackBy(leds, COSTUME_NUM_LEDS, 92);
    drawIdleLightning(now);
    drawIdleHeart(now, scale);
}

void renderCostume(unsigned long now) {
    unsigned long elapsed = now - modeStartedAt;

    if (pendingSync && (long)(now - pendingSyncAt) >= 0) {
        pendingSync = false;
        costumeMode = MODE_SYNC_STROBE;
        modeStartedAt = now;
        elapsed = 0;
    }

    switch (costumeMode) {
        case MODE_IDLE:
            renderIdle(now);
            break;

        case MODE_RED_EXPAND:
            fadeToBlackBy(leds, COSTUME_NUM_LEDS, 95);
            drawRedExpand(elapsed);
            if (elapsed >= RED_EXPAND_MS) {
                costumeMode = MODE_RED_RELEASE;
                modeStartedAt = now;
            }
            break;

        case MODE_RED_RELEASE:
            fadeToBlackBy(leds, COSTUME_NUM_LEDS, 95);
            drawRedRelease(elapsed);
            if (elapsed >= RED_RELEASE_MS) {
                costumeMode = MODE_SPARKLE_OUT;
                modeStartedAt = now;
            }
            break;

        case MODE_SPARKLE_OUT:
            fadeToBlackBy(leds, COSTUME_NUM_LEDS, 105);
            drawSparkleOut(elapsed);
            if (elapsed >= COSTUME_SPARKLE_OUT_MS) {
                costumeMode = MODE_DARK_WAIT;
                modeStartedAt = now;
            }
            break;

        case MODE_DARK_WAIT:
            fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
            break;

        case MODE_SYNC_STROBE:
            showTripleWhiteStrobe(elapsed);
            if (elapsed >= SYNC_STROBE_DURATION) {
                costumeMode = MODE_SYNC_HOLD;
                modeStartedAt = now;
            }
            break;

        case MODE_SYNC_HOLD:
            fadeToBlackBy(leds, COSTUME_NUM_LEDS, 70);
            drawSharedEffect(now, 255);
            if (elapsed >= COSTUME_SYNC_HOLD_MS) {
                costumeMode = MODE_SYNC_FADE_OUT;
                modeStartedAt = now;
            }
            break;

        case MODE_SYNC_FADE_OUT: {
            uint8_t progress = elapsed >= COSTUME_SYNC_FADE_OUT_MS
                ? 255
                : map(elapsed, 0, COSTUME_SYNC_FADE_OUT_MS - 1, 0, 255);
            fadeToBlackBy(leds, COSTUME_NUM_LEDS, 80);
            drawSharedEffect(now, 255 - ease8InOutQuad(progress));
            if (elapsed >= COSTUME_SYNC_FADE_OUT_MS) {
                costumeMode = MODE_SYNC_FADE_IN;
                modeStartedAt = now;
            }
            break;
        }

        case MODE_SYNC_FADE_IN: {
            uint8_t progress = elapsed >= COSTUME_SYNC_FADE_IN_MS
                ? 255
                : map(elapsed, 0, COSTUME_SYNC_FADE_IN_MS - 1, 0, 255);
            renderIdle(now, ease8InOutQuad(progress));
            if (elapsed >= COSTUME_SYNC_FADE_IN_MS) {
                costumeMode = MODE_IDLE;
                modeStartedAt = now;
            }
            break;
        }
    }

    for (uint16_t i = segments[SEGMENT_COUNT - 1].start + segments[SEGMENT_COUNT - 1].length; i < COSTUME_NUM_LEDS; i++) {
        leds[i] = CRGB::Black;
    }

    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    unsigned long serialWaitStartedAt = millis();
    while (!Serial && millis() - serialWaitStartedAt < 4000) {
        delay(10);
    }
    delay(300);

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.println();
    Serial.println("--- YARA COSTUME PROTOTYPE START ---");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("LEDs=%u, active=%u, power limit=%u mA, brightness=%u\n",
        COSTUME_NUM_LEDS,
        segments[SEGMENT_COUNT - 1].start + segments[SEGMENT_COUNT - 1].length,
        COSTUME_POWER_MILLIAMPS,
        COSTUME_BRIGHTNESS);

    FastLED.addLeds<WS2812B, COSTUME_LED_PIN, GRB>(leds, COSTUME_NUM_LEDS);
    FastLED.setBrightness(COSTUME_BRIGHTNESS);
    FastLED.setMaxPowerInVoltsAndMilliamps(COSTUME_POWER_VOLTAGE, COSTUME_POWER_MILLIAMPS);
    fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
    FastLED.show();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW init costume");
    } else {
        esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
        Serial.println("ESP-NOW costume actif: ecoute des activations de couronnes.");
    }

    modeStartedAt = millis();
    scheduleNextBolt(modeStartedAt);
}

void loop() {
    static unsigned long lastFrameAt = 0;
    unsigned long now = millis();

    if (now - lastFrameAt >= 20) {
        lastFrameAt = now;
        renderCostume(now);
    }
}
