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
#define COSTUME_PIXEL_COUNT_MODE false

#define SEGMENT_COUNT 7
#define HEART_SECTION_LEN 10
#define HEART_EDGE_SECTION_LEN 5
#define LIGHTNING_MAX_BOLTS 16
#define LIGHTNING_MIN_GAP_MS 250UL
#define LIGHTNING_MAX_GAP_MS 3000UL
#define LIGHTNING_MIN_TRAVEL_MS 420UL
#define LIGHTNING_MAX_TRAVEL_MS 1250UL
#define COSTUME_SHARED_SPARKLE_COUNT 14
#define COSTUME_SHARED_SPARKLE_MIN_GAP_MS 90UL
#define COSTUME_SHARED_SPARKLE_MAX_GAP_MS 260UL

#define RED_EXPAND_MS 1000UL
#define RED_RELEASE_MS 1000UL
#define COSTUME_SPARKLE_OUT_MS 5000UL
#define COSTUME_SYNC_HOLD_MS 10000UL
#define COSTUME_SYNC_FADE_OUT_MS 500UL
#define COSTUME_SYNC_FADE_IN_MS 500UL

CRGB leds[COSTUME_NUM_LEDS];

struct Segment {
    uint16_t start;
    uint16_t length;
};

const Segment segments[SEGMENT_COUNT] = {
    {0, 73},
    {73, 54},
    {127, 67},
    {194, 28},
    {222, 26},
    {248, 28},
    {276, 60}
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
    MODE_SYNC_FADE_IN,
    MODE_GLOBAL_HOLD
};

struct LightningBolt {
    bool active;
    unsigned long startedAt;
    unsigned long duration;
    int startPos;
    int targetPos;
};

struct CostumeSparkle {
    bool active;
    uint16_t pos;
    unsigned long startedAt;
    unsigned long duration;
};

CostumeMode costumeMode = MODE_IDLE;
LightningBolt bolts[LIGHTNING_MAX_BOLTS] = {};
CostumeSparkle sharedSparkles[COSTUME_SHARED_SPARKLE_COUNT] = {};
unsigned long modeStartedAt = 0;
unsigned long nextBoltAt[SEGMENT_COUNT] = {};
unsigned long nextSharedSparkleAt = 0;
unsigned long pendingSyncAt = 0;
bool pendingSync = false;
struct_message currentShowMessage = {};

uint16_t activeLedCount() {
    return segments[SEGMENT_COUNT - 1].start + segments[SEGMENT_COUNT - 1].length;
}

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
    bool segment4Start = pos >= segments[3].start && pos < segments[3].start + HEART_EDGE_SECTION_LEN;
    bool segment4End = pos <= segmentEnd(3) && pos > segmentEnd(3) - HEART_EDGE_SECTION_LEN;
    bool segment5Start = pos >= segments[4].start && pos < segments[4].start + HEART_EDGE_SECTION_LEN;
    bool segment5End = pos <= segmentEnd(4) && pos > segmentEnd(4) - HEART_EDGE_SECTION_LEN;
    bool segment6Start = pos >= segments[5].start && pos < segments[5].start + HEART_EDGE_SECTION_LEN;
    bool segment6End = pos <= segmentEnd(5) && pos > segmentEnd(5) - HEART_EDGE_SECTION_LEN;
    bool segment7 = pos >= segments[6].start && pos < segments[6].start + HEART_SECTION_LEN;
    return segment1 ||
        segment2 ||
        segment3Start ||
        segment3End ||
        segment4Start ||
        segment4End ||
        segment5Start ||
        segment5End ||
        segment6Start ||
        segment6End ||
        segment7;
}

void addLightningPixel(int pos, const CRGB& color, uint8_t scale) {
    if (isHeartPixel(pos)) {
        return;
    }

    addPixel(pos, color, scale);
}

void drawRedSection(int start, int direction, uint8_t pulseScale, uint8_t length = HEART_SECTION_LEN) {
    const uint8_t basePct[HEART_SECTION_LEN] = {18, 14, 11, 8, 6, 5, 4, 3, 2, 1};
    const uint8_t beatPct[HEART_SECTION_LEN] = {100, 92, 82, 70, 58, 46, 34, 24, 16, 9};

    for (uint8_t i = 0; i < length && i < HEART_SECTION_LEN; i++) {
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

    // Segments 4, 5 and 6: 5 pixels on both ends around the shoulder path.
    for (uint8_t s = 3; s <= 5; s++) {
        drawRedSection(segments[s].start, 1, pulse, HEART_EDGE_SECTION_LEN);
        drawRedSection(segmentEnd(s), -1, pulse, HEART_EDGE_SECTION_LEN);
    }

    // Segment 7: first 10 pixels from the chest toward the right wrist.
    drawRedSection(segments[6].start, 1, pulse);
}

void scheduleNextBolt(uint8_t segmentIndex, unsigned long now) {
    nextBoltAt[segmentIndex] = now + random16(LIGHTNING_MIN_GAP_MS, LIGHTNING_MAX_GAP_MS + 1);
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

void startSegmentBolt(uint8_t segmentIndex, unsigned long now) {
    switch (segmentIndex) {
        case 0:
            startBolt(now, segmentEnd(0), segments[0].start + HEART_SECTION_LEN);
            break;
        case 1:
            startBolt(now, segments[1].start, segmentEnd(1) - HEART_SECTION_LEN);
            break;
        case 2: {
            uint16_t middle = segments[2].start + segments[2].length / 2;
            startBolt(
                now,
                middle,
                (random8() & 1) ? segments[2].start + HEART_SECTION_LEN : segmentEnd(2) - HEART_SECTION_LEN);
            break;
        }
        case 3:
        case 4:
        case 5: {
            uint16_t middleLeft = segments[segmentIndex].start + segments[segmentIndex].length / 2 - 1;
            uint16_t middleRight = middleLeft + 1;
            bool towardStart = random8() & 1;
            startBolt(
                now,
                random8() & 1 ? middleLeft : middleRight,
                towardStart
                    ? segments[segmentIndex].start + HEART_EDGE_SECTION_LEN
                    : segmentEnd(segmentIndex) - HEART_EDGE_SECTION_LEN);
            break;
        }
        case 6:
            startBolt(now, segmentEnd(6), segments[6].start + HEART_SECTION_LEN);
            break;
        default:
            break;
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
    for (uint8_t s = 0; s < SEGMENT_COUNT; s++) {
        if (nextBoltAt[s] == 0) {
            scheduleNextBolt(s, now);
        }

        if ((long)(now - nextBoltAt[s]) >= 0) {
            startSegmentBolt(s, now);
            scheduleNextBolt(s, now);
        }
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
    drawExpandingRedOnSegment(3, progress);
    drawExpandingRedOnSegment(4, progress);
    drawExpandingRedOnSegment(5, progress);
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
    uint16_t clear4 = ((uint32_t)(segments[3].length / 2) * progress) / 255;
    uint16_t clear5 = ((uint32_t)(segments[4].length / 2) * progress) / 255;
    uint16_t clear6 = ((uint32_t)(segments[5].length / 2) * progress) / 255;
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

    uint16_t clearEdges[3] = {clear4, clear5, clear6};
    for (uint8_t s = 3; s <= 5; s++) {
        uint16_t clear = clearEdges[s - 3];
        uint16_t middleLeft = segments[s].start + segments[s].length / 2 - 1;
        uint16_t middleRight = middleLeft + 1;
        if (clear < segments[s].length / 2) {
            drawRedRange(segments[s].start + clear, middleLeft);
            drawRedRange(middleRight, segmentEnd(s) - clear);
        }
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
    uint8_t chance = map(progress, 0, 255, 72, 240);
    uint8_t burstCount = map(progress, 0, 255, 3, 9);
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
            spark.nscale8_video(map(progress, 0, 255, 150, 255));
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

bool isCrownEffect(uint8_t effectId) {
    switch (effectId) {
        case EFFECT_BREATH:
        case EFFECT_SPARKLE:
        case EFFECT_WAVE:
        case EFFECT_COLOR_CHASE:
        case EFFECT_THEATER_CHASE:
        case EFFECT_PORTAL:
        case EFFECT_GLITTER_RAIN:
        case EFFECT_RIPPLE:
        case EFFECT_CONSTELLATION:
        case EFFECT_VOGUE_POSE:
        case EFFECT_STORM:
        case EFFECT_QUEEN_AURA:
            return true;
        default:
            return false;
    }
}

void resetSharedSparkles() {
    for (uint8_t i = 0; i < COSTUME_SHARED_SPARKLE_COUNT; i++) {
        sharedSparkles[i].active = false;
    }
    nextSharedSparkleAt = 0;
}

void spawnSharedSparkle(unsigned long now) {
    for (uint8_t i = 0; i < COSTUME_SHARED_SPARKLE_COUNT; i++) {
        if (sharedSparkles[i].active) {
            continue;
        }

        sharedSparkles[i].active = true;
        sharedSparkles[i].pos = random16(activeLedCount());
        sharedSparkles[i].startedAt = now;
        sharedSparkles[i].duration = random16(520, 980);
        return;
    }
}

void updateSharedSparkles(unsigned long now) {
    for (uint8_t i = 0; i < COSTUME_SHARED_SPARKLE_COUNT; i++) {
        if (sharedSparkles[i].active && now - sharedSparkles[i].startedAt >= sharedSparkles[i].duration) {
            sharedSparkles[i].active = false;
        }
    }

    if (nextSharedSparkleAt == 0) {
        nextSharedSparkleAt = now + random16(COSTUME_SHARED_SPARKLE_MIN_GAP_MS, COSTUME_SHARED_SPARKLE_MAX_GAP_MS + 1);
    }

    if ((long)(now - nextSharedSparkleAt) >= 0) {
        spawnSharedSparkle(now);
        if (random8() < 70) {
            spawnSharedSparkle(now);
        }
        nextSharedSparkleAt = now + random16(COSTUME_SHARED_SPARKLE_MIN_GAP_MS, COSTUME_SHARED_SPARKLE_MAX_GAP_MS + 1);
    }
}

CRGB sharedSparkleColorAt(uint16_t pos, unsigned long now) {
    CRGB color = CRGB::Black;

    for (uint8_t i = 0; i < COSTUME_SHARED_SPARKLE_COUNT; i++) {
        if (!sharedSparkles[i].active || sharedSparkles[i].pos != pos) {
            continue;
        }

        unsigned long elapsed = now - sharedSparkles[i].startedAt;
        uint8_t scale = elapsed < 80
            ? map(elapsed, 0, 79, 40, 255)
            : map(elapsed, 80, sharedSparkles[i].duration - 1, 255, 0);
        CRGB spark = CRGB::White;
        spark.nscale8_video(scale);
        color += spark;
    }

    return color;
}

void drawCrownStyleSparkle(unsigned long now, uint8_t globalScale) {
    uint16_t count = activeLedCount();
    fadeToBlackBy(leds, COSTUME_NUM_LEDS, 30);

    CRGB background = colorFromHex(currentShowMessage.secondaryColor);
    background.nscale8_video(20);
    background.nscale8_video(scale8(currentShowMessage.intensity, globalScale));
    for (uint16_t i = 0; i < count; i++) {
        leds[i] += background;
    }

    uint8_t chance = currentShowMessage.speed == 0 ? 20 : currentShowMessage.speed;
    uint8_t sparkleCount = max<uint8_t>(1, count / NUM_LEDS);
    if (random8() < chance) {
        CRGB color = colorFromHex(currentShowMessage.primaryColor);
        color.nscale8_video(scale8(scale8(currentShowMessage.intensity, 185), globalScale));
        for (uint8_t i = 0; i < sparkleCount; i++) {
            leds[random16(count)] += color;
        }
    }
}

void drawCrownStyleGlitterRain(unsigned long now, uint8_t globalScale) {
    uint16_t count = activeLedCount();
    fadeToBlackBy(leds, COSTUME_NUM_LEDS, 25);

    uint16_t drift = ((now - modeStartedAt) * max<uint8_t>(currentShowMessage.speed, 55) / 850) % count;
    uint8_t chance = 95;
    uint8_t glitterCount = max<uint8_t>(1, count / NUM_LEDS);
    CRGB primary = colorFromHex(currentShowMessage.primaryColor);
    CRGB secondary = colorFromHex(currentShowMessage.secondaryColor);
    primary.nscale8_video(scale8(scale8(currentShowMessage.intensity, 185), globalScale));
    secondary.nscale8_video(scale8(scale8(currentShowMessage.intensity, 185), globalScale));

    if (random8() < chance) {
        for (uint8_t i = 0; i < glitterCount; i++) {
            uint16_t pos = (random16(count) + drift) % count;
            leds[pos] += (random8() & 1) ? primary : secondary;
        }
    }
}

void drawCrownStyleStorm(unsigned long now, uint8_t globalScale) {
    uint16_t count = activeLedCount();
    CRGB background = colorFromHex(currentShowMessage.secondaryColor);
    background.nscale8_video(scale8(45, globalScale));
    fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
    for (uint16_t i = 0; i < count; i++) {
        leds[i] = background;
    }

    uint8_t chance = currentShowMessage.speed == 0 ? 80 : currentShowMessage.speed;
    uint8_t strikeCount = max<uint8_t>(1, count / NUM_LEDS);
    if (random8() < chance) {
        CRGB strike = colorFromHex(currentShowMessage.primaryColor);
        strike.nscale8_video(scale8(scale8(currentShowMessage.intensity, 185), globalScale));
        CRGB trail = strike;
        trail.nscale8_video(120);

        for (uint8_t i = 0; i < strikeCount; i++) {
            uint16_t pos = random16(count);
            leds[pos] = strike;
            if (pos > 0) {
                leds[pos - 1] += trail;
            }
            if (pos + 1 < count) {
                leds[pos + 1] += trail;
            }
        }
    }
}

void drawCrownStyleRipple(unsigned long now, uint8_t globalScale) {
    fadeToBlackBy(leds, COSTUME_NUM_LEDS, 45);

    uint8_t stepDelay = 150 - max<uint8_t>(currentShowMessage.speed, 70);
    if (stepDelay < 22) {
        stepDelay = 22;
    }

    CRGB primary = colorFromHex(currentShowMessage.primaryColor);
    CRGB secondary = colorFromHex(currentShowMessage.secondaryColor);
    primary.nscale8_video(scale8(scale8(currentShowMessage.intensity, 185), globalScale));
    secondary.nscale8_video(scale8(scale8(currentShowMessage.intensity / 2, 185), globalScale));

    for (uint8_t s = 0; s < SEGMENT_COUNT; s++) {
        uint16_t length = segments[s].length;
        uint16_t emitterCount = length > 40 ? 3 : 2;
        uint16_t spacing = length / emitterCount;
        uint16_t radiusLimit = max<uint16_t>(1, spacing / 2);
        uint16_t radius = ((now - modeStartedAt) / stepDelay) % radiusLimit;
        bool collisionBeat = radius >= radiusLimit - 1;

        for (uint8_t e = 0; e < emitterCount; e++) {
            uint16_t center = (spacing / 2 + e * spacing + s * 3) % length;
            uint16_t bluePos = (center + radius) % length;
            uint16_t pinkPos = (center + length - radius) % length;

            leds[segments[s].start + bluePos] += primary;
            leds[segments[s].start + pinkPos] += secondary;

            if (radius > 0) {
                leds[segments[s].start + ((bluePos + length - 1) % length)] += CRGB(primary.r / 3, primary.g / 3, primary.b / 3);
                leds[segments[s].start + ((pinkPos + 1) % length)] += CRGB(secondary.r / 3, secondary.g / 3, secondary.b / 3);
            }

            if (collisionBeat) {
                CRGB flash = primary;
                flash += secondary;
                flash.nscale8_video(120);
                leds[segments[s].start + center] += flash;
            }
        }
    }
}

void drawCrownStyleBreath(unsigned long now, uint8_t globalScale) {
    uint16_t count = activeLedCount();
    uint8_t bpm = currentShowMessage.speed == 0 ? 1 : currentShowMessage.speed;
    uint8_t brightness = beatsin8(bpm, 10, currentShowMessage.intensity);
    CRGB color = colorFromHex(currentShowMessage.primaryColor);
    color.nscale8_video(scale8(scale8(brightness, 185), globalScale));

    fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
    for (uint16_t i = 0; i < count; i++) {
        leds[i] = color;
    }
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

            if (currentShowMessage.effectId == EFFECT_CONSTELLATION) {
                uint8_t t = (now - modeStartedAt) / 24;
                uint8_t sparkle = inoise8(pos * 70 + s * 29, t);

                color = secondary;
                color.nscale8_video(18);

                if (sparkle > 195) {
                    uint8_t starLevel = map(sparkle, 196, 255, 55, 255);
                    CRGB star = primary;
                    star.nscale8_video(starLevel);
                    if (sparkle > 232 && random8() < 24) {
                        star.nscale8_video(random8(35, 155));
                    }
                    color += star;
                }
            } else if (currentShowMessage.effectId == EFFECT_COLOR_CHASE) {
                uint8_t bpm = max<uint8_t>(currentShowMessage.speed, 75);
                uint8_t chaseOffset = ((now - modeStartedAt) * bpm / 700) % 4;
                uint8_t phase = (offset + chaseOffset) % 4;

                color = secondary;
                color.nscale8_video(35);
                if (phase == 0) {
                    color = primary;
                } else if (phase == 1) {
                    CRGB trail = primary;
                    trail.nscale8(90);
                    color += trail;
                }
            } else if (currentShowMessage.effectId == EFFECT_WAVE) {
                uint8_t bpm = currentShowMessage.speed == 0 ? 1 : currentShowMessage.speed;
                uint16_t point = beatsin8(bpm, 0, segments[s].length - 1);
                uint16_t distance = offset > point ? offset - point : point - offset;

                color = secondary;
                color.nscale8_video(70);
                if (distance == 0) {
                    color = primary;
                } else if (distance == 1) {
                    CRGB trail = primary;
                    trail.nscale8(120);
                    color += trail;
                } else if (distance == 2) {
                    CRGB trail = primary;
                    trail.nscale8(45);
                    color += trail;
                }
            } else if (currentShowMessage.effectId == EFFECT_VOGUE_POSE) {
                uint16_t stepDelay = 820 - max<uint8_t>(currentShowMessage.speed, 90) * 3;
                if (stepDelay < 120) {
                    stepDelay = 120;
                }
                uint8_t pose = ((now - modeStartedAt) / stepDelay) % 4;
                color = secondary;
                color.nscale8_video(30);
                if ((offset + pose) % 8 == 0 || (offset + pose) % 8 == 1) {
                    color = primary;
                }
            } else if (currentShowMessage.effectId == EFFECT_SPARKLE) {
                color = sharedSparkleColorAt(pos, now);
            } else if (currentShowMessage.effectId == EFFECT_QUEEN_AURA) {
                uint8_t pulse = beatsin8(max<uint8_t>(currentShowMessage.speed, 24), 90, 255);
                uint8_t blendAmount = sin8(offset * 14 + (now - modeStartedAt) / 18);
                uint8_t texture = inoise8(s * 42 + offset * 18, (now - modeStartedAt) / 20);
                blendAmount = qadd8(scale8(blendAmount, 190), scale8(texture, 65));
                color = blend(secondary, primary, blendAmount);
                color.nscale8_video(pulse);
                if (random8() < 3) {
                    color += CRGB(95, 95, 95);
                }
            } else if (currentShowMessage.effectId == EFFECT_THEATER_CHASE) {
                uint8_t stepDelay = 230 - max<uint8_t>(currentShowMessage.speed, 90);
                if (stepDelay < 35) {
                    stepDelay = 35;
                }
                uint8_t phase = ((now - modeStartedAt) / stepDelay) % 3;
                color = secondary;
                color.nscale8_video(25);
                if ((offset + phase) % 3 == 0) {
                    color = primary;
                }
            } else if (currentShowMessage.effectId == EFFECT_PORTAL) {
                uint16_t center = segments[s].length / 2;
                uint8_t maxRadius = max<uint8_t>(1, segments[s].length / 2);
                uint8_t radius = ((now - modeStartedAt) * max<uint8_t>(currentShowMessage.speed, 24) / 520) % maxRadius;
                uint16_t left = center > radius ? center - radius : 0;
                uint16_t right = min<uint16_t>(segments[s].length - 1, center + radius);
                uint16_t distanceLeft = offset > left ? offset - left : left - offset;
                uint16_t distanceRight = offset > right ? offset - right : right - offset;
                uint16_t distance = min<uint16_t>(distanceLeft, distanceRight);

                color = secondary;
                color.nscale8_video(45);

                if (distance == 0) {
                    color = primary;
                } else if (distance <= 4) {
                    CRGB trail = blend(primary, secondary, distance * 52);
                    trail.nscale8_video(255 - distance * 42);
                    color += trail;
                }

                if (offset == center || offset == center + 1) {
                    CRGB core = primary;
                    core.nscale8_video(70);
                    color += core;
                }
            } else if (currentShowMessage.effectId == EFFECT_PRISM || currentShowMessage.effectId == EFFECT_FINAL_RAVE) {
                color = CHSV(baseHue + offset * 4, 220, 255);
                if (currentShowMessage.effectId == EFFECT_FINAL_RAVE && ((now - modeStartedAt) % 940) < 65) {
                    color = CRGB::White;
                }
            } else if (currentShowMessage.effectId == EFFECT_PARTY_PULSE) {
                uint8_t texture = inoise8(offset * 36, (now - modeStartedAt) / 10);
                color = blend(CRGB(118, 20, 255), CRGB(255, 30, 190), texture);
                color = blend(color, CRGB(0, 210, 255), sin8(offset * 12 + baseHue) / 2);
            } else if (currentShowMessage.effectId == EFFECT_POMPON_SPARKLE) {
                color = CRGB(36, 0, 42);
                if (random8() < 55) {
                    color = random8() < 100 ? CRGB(255, 220, 120) : CRGB::White;
                }
            } else if (currentShowMessage.effectId == EFFECT_PUBLIC_WAVE) {
                uint16_t localLength = segments[s].length;
                uint16_t front = ((now - modeStartedAt) * max<uint8_t>(currentShowMessage.speed, 32) / 430) % localLength;
                uint16_t distance = offset > front ? offset - front : front - offset;
                color = distance < 12
                    ? blend(CRGB(255, 36, 190), CRGB(30, 170, 255), distance * 20)
                    : CRGB::Black;
            } else if (currentShowMessage.effectId == EFFECT_FINAL_FREEZE) {
                color = CRGB::White;
            }

            if (isCrownEffect(currentShowMessage.effectId)) {
                color.nscale8_video(185);
            }
            color.nscale8_video(scale8(currentShowMessage.intensity, globalScale));
            leds[pos] = color;
        }
    }
}

void handleActivationCommand(const struct_message& message) {
    for (uint8_t i = 0; i < LIGHTNING_MAX_BOLTS; i++) {
        bolts[i].active = false;
    }
    resetSharedSparkles();

    currentShowMessage = message;
    costumeMode = MODE_RED_EXPAND;
    modeStartedAt = millis();
    pendingSync = false;
}

void handleGlobalEffectCommand(const struct_message& message) {
    for (uint8_t i = 0; i < LIGHTNING_MAX_BOLTS; i++) {
        bolts[i].active = false;
    }
    resetSharedSparkles();

    currentShowMessage = message;
    costumeMode = MODE_GLOBAL_HOLD;
    modeStartedAt = millis();
    pendingSync = false;
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

    switch (incoming.command) {
        case COMMAND_ACTIVATE_CROWN:
            handleActivationCommand(incoming);
            break;

        case COMMAND_GLOBAL_EFFECT:
        case COMMAND_TEST_NETWORK:
            handleGlobalEffectCommand(incoming);
            break;

        case COMMAND_BLACKOUT:
            costumeMode = MODE_DARK_WAIT;
            pendingSync = false;
            break;

        case COMMAND_RESET_CROWNS:
        case COMMAND_RESTORE_FROM_BLACKOUT:
            costumeMode = MODE_IDLE;
            modeStartedAt = millis();
            pendingSync = false;
            for (uint8_t s = 0; s < SEGMENT_COUNT; s++) {
                scheduleNextBolt(s, modeStartedAt);
            }
            break;

        default:
            break;
    }
}

void renderIdle(unsigned long now, uint8_t scale = 255) {
    if (COSTUME_PIXEL_COUNT_MODE) {
        fill_solid(leds, COSTUME_NUM_LEDS, CRGB(0, 0, 80));
        return;
    }

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
                costumeMode = MODE_SYNC_STROBE;
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
                fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
            }
            break;

        case MODE_SYNC_HOLD:
            if (currentShowMessage.effectId == EFFECT_BREATH) {
                drawCrownStyleBreath(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_SPARKLE) {
                drawCrownStyleSparkle(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_GLITTER_RAIN) {
                drawCrownStyleGlitterRain(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_STORM) {
                drawCrownStyleStorm(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_RIPPLE) {
                drawCrownStyleRipple(now, 255);
            } else {
                fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
                drawSharedEffect(now, 255);
            }
            if (elapsed >= COSTUME_SYNC_HOLD_MS) {
                costumeMode = MODE_SYNC_FADE_OUT;
                modeStartedAt = now;
            }
            break;

        case MODE_SYNC_FADE_OUT: {
            uint8_t progress = elapsed >= COSTUME_SYNC_FADE_OUT_MS
                ? 255
                : map(elapsed, 0, COSTUME_SYNC_FADE_OUT_MS - 1, 0, 255);
            if (currentShowMessage.effectId == EFFECT_BREATH) {
                drawCrownStyleBreath(now, 255 - ease8InOutQuad(progress));
            } else if (currentShowMessage.effectId == EFFECT_SPARKLE) {
                drawCrownStyleSparkle(now, 255 - ease8InOutQuad(progress));
            } else if (currentShowMessage.effectId == EFFECT_GLITTER_RAIN) {
                drawCrownStyleGlitterRain(now, 255 - ease8InOutQuad(progress));
            } else if (currentShowMessage.effectId == EFFECT_STORM) {
                drawCrownStyleStorm(now, 255 - ease8InOutQuad(progress));
            } else if (currentShowMessage.effectId == EFFECT_RIPPLE) {
                drawCrownStyleRipple(now, 255 - ease8InOutQuad(progress));
            } else {
                fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
                drawSharedEffect(now, 255 - ease8InOutQuad(progress));
            }
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

        case MODE_GLOBAL_HOLD:
            if (currentShowMessage.effectId == EFFECT_BREATH) {
                drawCrownStyleBreath(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_SPARKLE) {
                drawCrownStyleSparkle(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_GLITTER_RAIN) {
                drawCrownStyleGlitterRain(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_STORM) {
                drawCrownStyleStorm(now, 255);
            } else if (currentShowMessage.effectId == EFFECT_RIPPLE) {
                drawCrownStyleRipple(now, 255);
            } else {
                fill_solid(leds, COSTUME_NUM_LEDS, CRGB::Black);
                drawSharedEffect(now, 255);
            }
            break;
    }

    if (!COSTUME_PIXEL_COUNT_MODE) {
        for (uint16_t i = segments[SEGMENT_COUNT - 1].start + segments[SEGMENT_COUNT - 1].length; i < COSTUME_NUM_LEDS; i++) {
            leds[i] = CRGB::Black;
        }
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
    for (uint8_t s = 0; s < SEGMENT_COUNT; s++) {
        scheduleNextBolt(s, modeStartedAt);
    }
}

void loop() {
    static unsigned long lastFrameAt = 0;
    unsigned long now = millis();

    if (now - lastFrameAt >= 20) {
        lastFrameAt = now;
        renderCostume(now);
    }
}
