#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>

#define COSTUME_LED_PIN 2
#define COSTUME_NUM_LEDS 500

// Prototype power limit: deliberately very low until the strip has proper power injection.
#define COSTUME_POWER_VOLTAGE 5
#define COSTUME_POWER_MILLIAMPS 180
#define COSTUME_BRIGHTNESS 24

#define HEART_DURATION 120000UL
#define MIN_HEART_PIXELS 8
#define MAX_HEART_PIXELS 400
#define COMET_MIN_GAP_MS 260UL
#define COMET_MAX_GAP_MS 1150UL
#define COMET_MIN_TRAVEL_MS 850UL
#define COMET_MAX_TRAVEL_MS 2300UL
#define IMPACT_BOOST_PIXELS 18
#define IMPACT_BOOST_MS 520UL

CRGB leds[COSTUME_NUM_LEDS];

unsigned long startedAt = 0;
unsigned long lastImpactAt = 0;

struct LightningBolt {
    bool active;
    unsigned long startedAt;
    unsigned long duration;
    uint16_t targetPos;
    uint8_t side;
};

LightningBolt bolts[4] = {};
unsigned long nextBoltAt = 0;

uint16_t centerStartForWidth(uint16_t width) {
    return (COSTUME_NUM_LEDS - width) / 2;
}

uint16_t currentHeartWidth(unsigned long elapsed) {
    if (elapsed >= HEART_DURATION) {
        return MAX_HEART_PIXELS;
    }

    uint32_t width = MIN_HEART_PIXELS +
        ((uint32_t)(MAX_HEART_PIXELS - MIN_HEART_PIXELS) * elapsed) / HEART_DURATION;

    return (uint16_t)width;
}

uint8_t heartbeatLevel(unsigned long now) {
    unsigned long phase = now % 1450UL;

    if (phase < 120) {
        return map(phase, 0, 119, 20, 120);
    }
    if (phase < 250) {
        return map(phase, 120, 249, 120, 8);
    }
    if (phase < 390) {
        return map(phase, 250, 389, 8, 95);
    }
    if (phase < 620) {
        return map(phase, 390, 619, 95, 0);
    }

    return 0;
}

void addCometPixel(int pos, const CRGB& color, uint8_t scale) {
    if (pos < 0 || pos >= COSTUME_NUM_LEDS) {
        return;
    }

    CRGB scaled = color;
    scaled.nscale8_video(scale);
    leds[pos] += scaled;
}

void scheduleNextBolt(unsigned long now) {
    nextBoltAt = now + random16(COMET_MIN_GAP_MS, COMET_MAX_GAP_MS);
}

void startBolt(unsigned long now, uint16_t heartStart, uint16_t heartEnd) {
    for (uint8_t i = 0; i < 4; i++) {
        if (bolts[i].active) {
            continue;
        }

        bolts[i].active = true;
        bolts[i].startedAt = now;
        bolts[i].duration = random16(COMET_MIN_TRAVEL_MS, COMET_MAX_TRAVEL_MS);
        bolts[i].side = random8(2);
        bolts[i].targetPos = bolts[i].side == 0 ? heartStart - 1 : heartEnd + 1;
        return;
    }
}

uint8_t easeInCubic8(uint8_t value) {
    uint16_t squared = ((uint16_t)value * value) / 255;
    return (uint8_t)((squared * value) / 255);
}

void drawBolt(const LightningBolt& bolt, unsigned long now) {
    unsigned long elapsed = now - bolt.startedAt;
    uint8_t progress = elapsed >= bolt.duration
        ? 255
        : map(elapsed, 0, bolt.duration - 1, 0, 255);
    uint8_t eased = easeInCubic8(progress);

    int startPos = bolt.side == 0 ? 0 : COSTUME_NUM_LEDS - 1;
    int targetPos = bolt.targetPos;
    int pos = startPos + ((int32_t)(targetPos - startPos) * eased) / 255;
    int direction = bolt.side == 0 ? -1 : 1;

    CRGB boltColor = CRGB(170, 220, 255);
    for (uint8_t i = 0; i < 20; i++) {
        uint8_t scale = 255 - i * 11;
        addCometPixel(pos + direction * i, boltColor, scale);
    }

    addCometPixel(pos, CRGB::White, 255);

    if (random8() < 90) {
        int branch = pos + direction * random8(8, 45);
        addCometPixel(branch, CRGB::White, random8(70, 170));
    }
}

void drawComets(unsigned long now, uint16_t heartStart, uint16_t heartEnd) {
    if (nextBoltAt == 0) {
        scheduleNextBolt(now);
    }

    if ((long)(now - nextBoltAt) >= 0) {
        startBolt(now, heartStart, heartEnd);
        scheduleNextBolt(now);
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (!bolts[i].active) {
            continue;
        }

        if (now - bolts[i].startedAt >= bolts[i].duration) {
            bolts[i].active = false;
            lastImpactAt = now;
            continue;
        }

        drawBolt(bolts[i], now);
    }
}

void drawHeart(unsigned long now, uint16_t baseWidth) {
    uint16_t width = baseWidth;
    if (now - lastImpactAt < IMPACT_BOOST_MS) {
        uint8_t impactProgress = map(now - lastImpactAt, 0, IMPACT_BOOST_MS - 1, 255, 0);
        width = min<uint16_t>(MAX_HEART_PIXELS, width + scale8(IMPACT_BOOST_PIXELS, impactProgress));
    }

    uint16_t start = centerStartForWidth(width);
    uint16_t end = start + width - 1;
    uint8_t pulse = heartbeatLevel(now);

    for (uint16_t i = start; i <= end && i < COSTUME_NUM_LEDS; i++) {
        uint16_t distanceFromCenter = abs((int)i - (int)(COSTUME_NUM_LEDS / 2));
        uint8_t edgeFalloff = distanceFromCenter > width / 2 ? 0 : 255 - scale8(distanceFromCenter, 190);
        uint8_t brightness = scale8(pulse, max<uint8_t>(80, edgeFalloff));
        CRGB red = CRGB(120, 0, 10);
        red.nscale8_video(brightness);
        leds[i] += red;
    }
}

void renderCostume(unsigned long now) {
    unsigned long elapsed = now - startedAt;
    uint16_t baseWidth = currentHeartWidth(elapsed);
    uint16_t heartStart = centerStartForWidth(baseWidth);
    uint16_t heartEnd = heartStart + baseWidth - 1;

    fadeToBlackBy(leds, COSTUME_NUM_LEDS, 86);
    drawComets(elapsed, heartStart, heartEnd);
    drawHeart(now, baseWidth);

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

    Serial.println();
    Serial.println("--- YARA COSTUME PROTOTYPE START ---");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("LEDs=%u, power limit=%u mA, brightness=%u\n",
        COSTUME_NUM_LEDS,
        COSTUME_POWER_MILLIAMPS,
        COSTUME_BRIGHTNESS);

    FastLED.addLeds<WS2812B, COSTUME_LED_PIN, GRB>(leds, COSTUME_NUM_LEDS);
    FastLED.setBrightness(COSTUME_BRIGHTNESS);
    FastLED.setMaxPowerInVoltsAndMilliamps(COSTUME_POWER_VOLTAGE, COSTUME_POWER_MILLIAMPS);
    fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
    FastLED.show();

    startedAt = millis();
}

void loop() {
    static unsigned long lastFrameAt = 0;
    unsigned long now = millis();

    if (now - lastFrameAt >= 20) {
        lastFrameAt = now;
        renderCostume(now);
    }
}
