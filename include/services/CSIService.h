#ifndef CSI_SERVICE_H
#define CSI_SERVICE_H

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>

class MQTTService;

/**
 * WiFi CSI Service — captures Channel State Information alongside Ethernet
 *
 * WiFi runs in STA mode purely as a CSI sensor (packet capture).
 * Network connectivity stays on Ethernet (PoE). WiFi does NOT need
 * internet access — it only needs to associate with an AP for CSI frames.
 *
 * Based on ESPectre by Francesco Pace (GPLv3).
 * Ported to Arduino/PlatformIO standalone for POE-2412 radar+CSI fusion.
 *
 * Features ported from ESPectre:
 *   - HT20/11n WiFi forcing for consistent 64 subcarrier CSI
 *   - STBC and short HT20 packet handling
 *   - Hampel outlier filter (MAD-based)
 *   - Low-pass filter (1st order Butterworth IIR)
 *   - CV normalization (gain-invariant turbulence for ESP32 without AGC lock)
 *   - Breathing-aware presence hold (prevents dropping stationary person)
 *   - DNS traffic generator (UDP queries to gateway for consistent CSI rate)
 *   - Two-pass variance calculation (numerically stable on float32)
 */
class CSIService {
public:
    CSIService();

    void begin(const char* ssid, const char* password,
               MQTTService* mqtt, const char* topicPrefix);
    void update();

    bool isActive() const { return _active; }

    // Accessors
    float getTurbulence() const { return _lastTurbulence; }
    float getPhaseTurbulence() const { return _lastPhaseTurb; }
    float getRatioTurbulence() const { return _lastRatioTurb; }
    float getBreathingScore() const;
    float getCompositeScore() const;
    bool  getMotionState() const { return _motionState; }
    float getVariance() const { return _runningVariance; }
    uint32_t getPacketCount() const { return _totalPackets; }

    // Runtime stats
    float getPacketRate() const { return _packetRate; }
    int   getWifiRSSI() const;
    String getWifiSSID() const;
    bool  isIdleInitialized() const { return _idleInitialized; }
    bool  isTrafficGenRunning() const { return _trafficGenRunning.load(); }

    // Configuration getters
    uint16_t getWindowSize()       const { return _windowSize; }
    float    getThreshold()        const { return _threshold; }
    float    getHysteresis()       const { return _hysteresis; }
    uint32_t getPublishInterval()  const { return _publishIntervalMs; }
    uint32_t getTrafficRate()      const { return _trafficRatePps; }
    uint16_t getTrafficPort()      const { return _trafficPort; }
    bool     getTrafficICMP()      const { return _trafficICMP; }

    // Configuration setters
    void setPublishInterval(uint32_t ms) { if (ms < 100) ms = 100; if (ms > 60000) ms = 60000; _publishIntervalMs = ms; }
    void setThreshold(float thr)         { if (thr < 0.001f) thr = 0.001f; if (thr > 100.0f) thr = 100.0f; _threshold = thr; }
    void setHysteresis(float hys)        { if (hys < 0.1f) hys = 0.1f; if (hys > 0.99f) hys = 0.99f; _hysteresis = hys; }
    void setWindowSize(uint16_t ws);
    void setTrafficRate(uint32_t pps);
    void setTrafficPort(uint16_t port);
    void setTrafficICMP(bool icmp);

    // Diagnostics actions
    void resetIdleBaseline();
    void forceReconnect();
    void calibrateThreshold(uint32_t durationMs = 10000);
    bool isCalibrating() const { return _calibrating; }
    float getCalibrationProgress() const;

private:
    static void _csiCallback(void* ctx, wifi_csi_info_t* info);
    void _processCSI(wifi_csi_info_t* info);
    void _publishMQTT();
    void _updateMotionState();
    void _initWiFiForCSI(const char* ssid, const char* password);
    void _startTrafficGen();
    void _stopTrafficGen();
    static void _trafficGenTask(void* arg);

    // Configuration
    uint16_t _windowSize = 75;
    float _threshold = 0.5f;
    float _hysteresis = 0.7f;
    uint32_t _publishIntervalMs = 1000;
    uint32_t _trafficRatePps = 100;
    uint16_t _trafficPort = 7;      // Default: echo port (7), alt: 53 (DNS)
    bool     _trafficICMP = false;   // ICMP echo (ping) mode — better response rate

    // HT20 subcarrier selection (12 subcarriers, avoiding guard bands <11 and >52, DC=32)
    static constexpr uint8_t NUM_SUBCARRIERS = 12;
    static constexpr uint8_t SUBCARRIERS[12] = {12, 14, 16, 18, 20, 24, 28, 36, 40, 44, 48, 52};

    // HT20 constants
    static constexpr uint16_t HT20_CSI_LEN = 128;        // 64 SC × 2 bytes
    static constexpr uint16_t HT20_CSI_LEN_DOUBLE = 256;  // STBC doubled
    static constexpr uint16_t HT20_CSI_LEN_SHORT = 114;   // 57 SC × 2 bytes
    static constexpr uint8_t  HT20_SHORT_LEFT_PAD = 8;    // 4 SC × 2 bytes guard padding

    // Circular turbulence buffer
    float* _turbBuffer = nullptr;
    uint16_t _bufIndex = 0;
    uint16_t _bufCount = 0;

    // Running variance (two-pass on publish, Welford's incremental for interim)
    float _runningMean = 0.0f;
    float _runningM2 = 0.0f;
    float _runningVariance = 0.0f;

    // Per-packet signals
    float _lastTurbulence = 0.0f;
    float _lastPhaseTurb = 0.0f;
    float _lastRatioTurb = 0.0f;
    float _lastAmpSum = 0.0f;  // for idle baseline tracking

    // Hampel filter state (MAD-based outlier removal)
    static constexpr uint8_t HAMPEL_WINDOW = 7;
    static constexpr float HAMPEL_THRESHOLD = 5.0f;
    static constexpr float MAD_SCALE = 1.4826f;
    struct {
        float buffer[11];  // max window size
        uint8_t index = 0;
        uint8_t count = 0;
    } _hampelState;

    // Low-pass filter state (1st order Butterworth IIR)
    struct {
        float b0 = 0;
        float a1 = 0;
        float x_prev = 0;
        float y_prev = 0;
        bool initialized = false;
    } _lowpassState;

    // Breathing bandpass filter state
    struct {
        float hp_x_prev = 0, hp_y_prev = 0;
        float lp_x_prev = 0, lp_y_prev = 0;
        float energy = 0;
        bool initialized = false;
    } _breathFilter;

    // Temporal smoothing
    static constexpr uint8_t SMOOTH_WINDOW = 6;
    static constexpr uint8_t SMOOTH_ENTER = 4;  // espectre: 4/6 to enter MOTION
    static constexpr uint8_t SMOOTH_EXIT = 5;   // espectre: 5/6 to exit
    uint8_t _smoothHistory = 0;
    uint8_t _smoothCount = 0;

    // Motion state
    bool _motionState = false;

    // Breathing-aware presence hold
    uint16_t _breathHoldCount = 0;
    static constexpr uint16_t BREATH_HOLD_MAX = 300; // ~5 min at 1s publish

    // Idle baselines (EMA)
    float _idleMeanTurb = 0;
    float _idleMeanPhase = 0;
    float _idleAmpBaseline = 0;
    bool _idleInitialized = false;

    // Timing
    uint32_t _totalPackets = 0;
    uint32_t _windowPackets = 0;
    uint32_t _lastPublishMs = 0;
    float    _packetRate = 0.0f;
    bool     _reconnectRequested = false;

    // Calibration
    bool     _calibrating = false;
    uint32_t _calibStartMs = 0;
    uint32_t _calibDurationMs = 0;
    float    _calibVarSum = 0.0f;
    uint32_t _calibSamples = 0;

    // Traffic generator
    TaskHandle_t _trafficGenHandle = nullptr;
    int _trafficGenSock = -1;
    std::atomic<bool> _trafficGenRunning{false};

    // State
    bool _active = false;
    MQTTService* _mqtt = nullptr;
    char _topicPrefix[64] = {};

    // Publish topics
    char _tMotion[80] = {};
    char _tTurbulence[80] = {};
    char _tVariance[80] = {};
    char _tPhaseTurb[80] = {};
    char _tRatioTurb[80] = {};
    char _tBreathing[80] = {};
    char _tComposite[80] = {};
    char _tPackets[80] = {};
};

#endif // CSI_SERVICE_H
