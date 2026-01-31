#include <btd700/btd700_c.h>
#include <cstdio>
#include <cstdlib>
#include <sys/select.h>
#include <unistd.h>

static void check(const btd700_error_t err, const char* context, const int exit = 1) {
    if (err != BTD700_OK) {
        std::fprintf(stderr, "%s: %s\n", context, btd700_error_string(err));
        if (exit) {
            std::exit(1);
        }
    }
}

static void showDeviceInfo(btd700_driver_t* drv) {
    btd700_device_info_t info;
    check(btd700_driver_device_info(drv, &info), "device_info");

    btd700_firmware_version_t fw;
    check(btd700_driver_firmware_version(drv, &fw), "firmware_version");

    std::printf("=== DEVICE INFO ===\n");
    std::printf("Manufacturer : %s\n", info.manufacturer);
    std::printf("Product      : %s\n", info.product);
    std::printf("Serial       : %s\n", info.serial);
    std::printf("Firmware     : %u.%u.%u\n", fw.major, fw.minor, fw.build);
}

static void showStatus(btd700_driver_t* drv) {
    btd700_dongle_state_t state;
    check(btd700_driver_state(drv, &state), "state");
    std::printf("State        : %s\n", btd700_dongle_state_string(state));

    btd700_le_audio_state_t le;
    check(btd700_driver_le_audio_state(drv, &le), "le_audio_state");
    std::printf("LE Audio     : %s\n", btd700_le_audio_state_string(le));

    int gaming = 0;
    check(btd700_driver_is_gaming_available(drv, &gaming), "is_gaming_available", 0);
    std::printf("Gaming       : %s\n", gaming ? "available" : "not available");
}

static void showAudioConfig(btd700_driver_t* drv) {
    btd700_audio_config_t cfg;
    check(btd700_driver_audio_config(drv, &cfg), "audio_config");
    std::printf("Audio mode   : %s\n", btd700_audio_mode_string(cfg.mode));
    std::printf("BT mode    : %s\n", btd700_transport_mode_string(cfg.transport));

    btd700_sink_mode_t sink;
    check(btd700_driver_sink_transport(drv, &sink), "sink_transport");
    std::printf("BT sink         : %s\n", btd700_sink_mode_string(sink));

    btd700_audio_quality_t q;
    check(btd700_driver_audio_quality(drv, &q), "audio_quality");
    std::printf("Frequency    : %s\n", btd700_audio_frequency_string(q.frequency));
    std::printf("Resolution   : %s\n", btd700_audio_resolution_string(q.resolution));
}

static void showCodecs(btd700_driver_t* drv) {
    uint16_t supported, active;
    check(btd700_driver_supported_codecs(drv, &supported), "supported_codecs");
    check(btd700_driver_active_codec(drv, &active), "active_codec");

    std::printf("=== SUPPORTED CODECS ===\n");
    for (size_t i = 0; i <= 5; ++i) {
        if (supported & (1 << i)) {
            const char* name = btd700_codec_string(static_cast<btd700_codec_t>(i));
            const bool is_active = (active & (1 << i)) != 0;
            std::printf("  %s %s\n", is_active ? "[*]" : "[ ]", name);
        }
    }
}

static void showBroadcastInfo(btd700_driver_t* drv) {
    btd700_broadcast_info_t info;
    check(btd700_driver_broadcast_info(drv, &info), "broadcast_info");
    std::printf("=== BROADCAST ===\n");
    std::printf("Broadcast    : %s\n", btd700_broadcast_state_string(info.state));
    std::printf("Encryption   : %s\n", btd700_broadcast_encryption_string(info.encryption));
    std::printf("Quality      : %s\n", btd700_broadcast_quality_string(info.quality));

    char name[256];
    check(btd700_driver_broadcast_name(drv, name, sizeof(name)), "broadcast_name");
    std::printf("Name         : %s\n", name);
}

static void changeAudioMode(btd700_driver_t* drv) {
    std::printf("\nSelect audio mode:\n");
    std::printf("  0) High Quality\n  1) Gaming\n  2) Broadcast\n> ");
    char buf[16];
    if (!std::fgets(buf, sizeof(buf), stdin)) return;
    int mode = std::atoi(buf);
    if (mode < 0 || mode > 2) return;

    check(btd700_driver_set_audio_mode(drv,
        static_cast<btd700_audio_mode_t>(mode),
        BTD700_TRANSPORT_CLASSIC), "set_audio_mode");
    std::printf("Audio mode set.\n");
}

static void changeCodec(btd700_driver_t* drv) {
    uint16_t supported;
    check(btd700_driver_supported_codecs(drv, &supported), "supported_codecs");

    std::printf("\nSelect codec:\n");
    for (int i = 0; i <= 5; ++i) {
        if (supported & (1 << i)) {
            std::printf("  %d) %s\n", i, btd700_codec_string(static_cast<btd700_codec_t>(i)));
        }
    }
    std::printf("> ");

    char buf[16];
    if (!std::fgets(buf, sizeof(buf), stdin)) return;
    const int codec = std::atoi(buf);
    if (codec < 0 || codec > 5 || !(supported & (1 << codec))) {
        std::printf("Invalid codec.\n");
        return;
    }

    check(btd700_driver_set_codec(drv,
        static_cast<btd700_codec_t>(codec)), "set_codec");
    std::printf("Codec set.\n");
}

static void onEvent(const btd700_event_t* event, void*) {
    static const char* event_names[] = {
        "StateChanged", "AudioModeChanged", "CodecChanged",
        "LEAudioStateChanged", "AudioQualityChanged",
        "SinkTransportChanged", "GamingAvailabilityChanged",
    };

    const char* name = (event->type >= 0 && event->type <= 6)
        ? event_names[event->type] : "Unknown";
    std::printf("%s (%zu bytes)\n", name, event->data_len);
}

static bool stdin_has_input() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 0};
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

static void monitorEvents(btd700_driver_t* drv) {
    btd700_driver_set_event_callback(drv, onEvent, nullptr);
    std::printf("Monitoring events (press Enter to stop)...\n");

    while (!stdin_has_input()) {
        btd700_driver_poll_events(drv, 100);
    }

    char buf[16];
    std::fgets(buf, sizeof(buf), stdin);

    btd700_driver_set_event_callback(drv, nullptr, nullptr);
    std::printf("Monitoring stopped.\n");
}

int main() {
    btd700_driver_t* drv = nullptr;
    check(btd700_driver_create(&drv), "create");
    check(btd700_driver_connect(drv), "connect");

    bool running = true;
    while (running) {
        std::printf("\n--- BTD 700 ---\n");
        std::printf("1) Device info \n");
        std::printf("2) Change audio mode\n");
        std::printf("3) Change codec\n");
        std::printf("4) Trigger connect\n");
        std::printf("5) Trigger disconnect\n");
        std::printf("m) Monitor events\n");
        std::printf("q) Quit\n> ");

        char line[16];
        if (!std::fgets(line, sizeof(line), stdin)) break;

        switch (line[0]) {
            case '1':
                showDeviceInfo(drv);
                showStatus(drv);
                showAudioConfig(drv);
                showCodecs(drv);
                showBroadcastInfo(drv);
                break;
            case '2': changeAudioMode(drv); break;
            case '3': changeCodec(drv); break;
            case '4': btd700_driver_trigger_connect(drv); break;
            case '5': btd700_driver_trigger_disconnect(drv); break;
            case 'm': case 'M': monitorEvents(drv); break;
            case 'q': case 'Q': running = false; break;
            default: break;
        }
    }

    btd700_driver_disconnect(drv);
    btd700_driver_destroy(drv);
    return 0;
}
