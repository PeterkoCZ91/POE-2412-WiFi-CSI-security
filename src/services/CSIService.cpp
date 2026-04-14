#include "services/CSIService.h"
#include "services/MQTTService.h"
#include "debug.h"
#include <WiFi.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "lwip/sockets.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_wifi.h"

// Breathing bandpass IIR coefficients (HP 0.08Hz + LP 0.6Hz @ ~100Hz)
static constexpr float BREATH_HP_B0 = 0.99749f;
static constexpr float BREATH_HP_A1 = -0.99498f;
static constexpr float BREATH_LP_B0 = 0.01850f;
static constexpr float BREATH_LP_A1 = -0.96300f;
static constexpr float BREATH_ENERGY_ALPHA = 0.00333f;

// Low-pass default: 11 Hz cutoff at 100 Hz sample rate
static constexpr float LOWPASS_CUTOFF_HZ = 11.0f;
static constexpr float LOWPASS_SAMPLE_RATE_HZ = 100.0f;

static const char* TAG = "CSI";

// Minimal DNS query for traffic generation (17 bytes)
static const uint8_t DNS_QUERY[] = {
    0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01
};

// ============================================================================
// Helpers
// ============================================================================

static float calculateMedian(float* arr, size_t size) {
    if (size == 0) return 0.0f;
    std::sort(arr, arr + size);
    if (size % 2 == 0) return (arr[size/2 - 1] + arr[size/2]) / 2.0f;
    return arr[size/2];
}

// ============================================================================
// Static callback trampoline
// ============================================================================

constexpr uint8_t CSIService::SUBCARRIERS[12];

void CSIService::_csiCallback(void* ctx, wifi_csi_info_t* info) {
    if (ctx && info && info->buf && info->len > 0) {
        static_cast<CSIService*>(ctx)->_processCSI(info);
    }
}

// ============================================================================
// Constructor / Init
// ============================================================================

CSIService::CSIService() {}

void CSIService::_initWiFiForCSI(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);

    // Force 802.11n protocol for HT20 CSI (64 subcarriers)
    esp_err_t ret = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11N);
    if (ret != ESP_OK) {
        // Fallback to b/g/n
        ret = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
        if (ret == ESP_OK) Serial.println("[CSI] 11n-only not accepted, using b/g/n fallback");
    }

    // Force HT20 bandwidth for consistent 64 subcarriers
    ret = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (ret != ESP_OK) {
        Serial.printf("[CSI] WARNING: Failed to set HT20 bandwidth: 0x%x\n", ret);
    }

    // Initialize internal WiFi structures for CSI (required even when false)
    esp_wifi_set_promiscuous(false);

    WiFi.begin(ssid, password);
    Serial.printf("[CSI] Connecting WiFi to %s (HT20/11n) for CSI capture...\n", ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[CSI] WARNING: WiFi not connected, will retry in background");
    } else {
        Serial.printf("[CSI] WiFi connected (IP: %s, RSSI: %d)\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
}

void CSIService::begin(const char* ssid, const char* password,
                       MQTTService* mqtt, const char* topicPrefix) {
    _mqtt = mqtt;
    strncpy(_topicPrefix, topicPrefix, sizeof(_topicPrefix) - 1);

    // Build MQTT topics
    snprintf(_tMotion,     sizeof(_tMotion),     "%s/motion",          _topicPrefix);
    snprintf(_tTurbulence, sizeof(_tTurbulence), "%s/turbulence",      _topicPrefix);
    snprintf(_tVariance,   sizeof(_tVariance),   "%s/variance",        _topicPrefix);
    snprintf(_tPhaseTurb,  sizeof(_tPhaseTurb),  "%s/phase_turbulence",_topicPrefix);
    snprintf(_tRatioTurb,  sizeof(_tRatioTurb),  "%s/ratio_turbulence",_topicPrefix);
    snprintf(_tBreathing,  sizeof(_tBreathing),  "%s/breathing_score", _topicPrefix);
    snprintf(_tComposite,  sizeof(_tComposite),  "%s/composite_score", _topicPrefix);
    snprintf(_tPackets,    sizeof(_tPackets),     "%s/packets",        _topicPrefix);

    // Allocate turbulence buffer
    _turbBuffer = new (std::nothrow) float[_windowSize];
    if (!_turbBuffer) {
        Serial.println("[CSI] ERROR: Failed to allocate turbulence buffer");
        return;
    }
    memset(_turbBuffer, 0, _windowSize * sizeof(float));

    // Initialize low-pass filter coefficients (bilinear transform)
    float wc = tanf(M_PI * LOWPASS_CUTOFF_HZ / LOWPASS_SAMPLE_RATE_HZ);
    float k = 1.0f + wc;
    _lowpassState.b0 = wc / k;
    _lowpassState.a1 = (wc - 1.0f) / k;

    // Initialize WiFi with HT20/11n forcing
    _initWiFiForCSI(ssid, password);

    // Configure and enable CSI
    wifi_csi_config_t csi_config = {};
    csi_config.lltf_en = true;
    csi_config.htltf_en = true;
    csi_config.stbc_htltf2_en = true;
    csi_config.ltf_merge_en = true;
    csi_config.channel_filter_en = false;

    esp_wifi_set_csi_config(&csi_config);
    esp_wifi_set_csi_rx_cb(_csiCallback, this);
    esp_wifi_set_csi(true);

    _active = true;
    Serial.printf("[CSI] CSI capture enabled (window=%d, threshold=%.2f)\n",
                  _windowSize, _threshold);

    // Start traffic generator for consistent CSI packet rate
    if (WiFi.status() == WL_CONNECTED) {
        _startTrafficGen();
    }
}

void CSIService::setWindowSize(uint16_t ws) {
    if (ws < 10) ws = 10;
    if (ws > 200) ws = 200;
    if (_turbBuffer) delete[] _turbBuffer;
    _windowSize = ws;
    _turbBuffer = new (std::nothrow) float[_windowSize];
    if (_turbBuffer) memset(_turbBuffer, 0, _windowSize * sizeof(float));
    _bufIndex = 0;
    _bufCount = 0;
    _runningMean = 0;
    _runningM2 = 0;
}

void CSIService::setTrafficRate(uint32_t pps) {
    if (pps < 10) pps = 10;
    if (pps > 500) pps = 500;
    _trafficRatePps = pps;
    // Restart traffic gen if running
    if (_trafficGenRunning.load()) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

void CSIService::setTrafficPort(uint16_t port) {
    if (port == 0) port = 7;
    _trafficPort = port;
    if (_trafficGenRunning.load() && !_trafficICMP) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

void CSIService::setTrafficICMP(bool icmp) {
    if (_trafficICMP == icmp) return;
    _trafficICMP = icmp;
    if (_trafficGenRunning.load()) {
        _stopTrafficGen();
        _startTrafficGen();
    }
}

// ============================================================================
// Traffic Generator (DNS UDP to gateway:53)
// ============================================================================

void CSIService::_startTrafficGen() {
    if (_trafficGenRunning.load()) return;

    esp_netif_t* esp_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!esp_netif) { DBG("CSI", "TrafficGen: no WiFi netif"); return; }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(esp_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        DBG("CSI", "TrafficGen: WiFi has no IP — disabled");
        return;
    }

    _trafficGenRunning.store(true);

    BaseType_t result = xTaskCreate(
        _trafficGenTask, "csi_traffic", 4096, this, 5, &_trafficGenHandle);
    if (result != pdPASS) {
        DBG("CSI", "TrafficGen: task create failed");
        _trafficGenRunning.store(false);
        return;
    }

    DBG("CSI", "TrafficGen: %u pps via lwIP %s port=%u (WiFi)",
        _trafficRatePps, _trafficICMP ? "ICMP" : "UDP", _trafficPort);
}

void CSIService::_stopTrafficGen() {
    if (!_trafficGenRunning.load()) return;
    _trafficGenRunning.store(false);

    if (_trafficGenHandle) {
        for (int i = 0; i < 10 && eTaskGetState(_trafficGenHandle) != eDeleted; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        _trafficGenHandle = nullptr;
    }

    DBG("CSI", "TrafficGen stopped");
}

// ICMP echo reply discard callback (we don't need replies, just TX traffic for CSI)
static uint8_t _icmpRecvCb(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    pbuf_free(p);
    return 1; // consumed
}

void CSIService::_trafficGenTask(void* arg) {
    CSIService* svc = static_cast<CSIService*>(arg);
    if (!svc) { vTaskDelete(NULL); return; }

    // Get WiFi lwIP netif and target IP
    esp_netif_t* esp_nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (!esp_nif || esp_netif_get_ip_info(esp_nif, &ip_info) != ESP_OK) {
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    // Get raw lwIP netif from esp_netif — this is the key to interface-specific sending
    struct netif* wifi_netif = (struct netif*)esp_netif_get_netif_impl(esp_nif);
    if (!wifi_netif) {
        DBG("CSI", "TrafficGen: failed to get lwIP netif");
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    uint32_t target_ip = ip_info.gw.addr;
    if (target_ip == 0) {
        target_ip = (ip_info.ip.addr & ip_info.netmask.addr) | PP_HTONL(0x00000001UL);
    }

    ip_addr_t src_addr = {};
    src_addr.type = IPADDR_TYPE_V4;
    src_addr.u_addr.ip4.addr = ip_info.ip.addr;

    ip_addr_t dst_addr = {};
    dst_addr.type = IPADDR_TYPE_V4;
    dst_addr.u_addr.ip4.addr = target_ip;

    bool useICMP = svc->_trafficICMP;
    uint16_t port = svc->_trafficPort;

    // Create either UDP PCB or ICMP raw PCB
    struct udp_pcb* udp_pcb_ptr = nullptr;
    struct raw_pcb* raw_pcb_ptr = nullptr;

    LOCK_TCPIP_CORE();
    if (useICMP) {
        raw_pcb_ptr = raw_new(IP_PROTO_ICMP);
        if (raw_pcb_ptr) {
            raw_bind(raw_pcb_ptr, &src_addr);
            raw_recv(raw_pcb_ptr, _icmpRecvCb, nullptr);
            raw_bind_netif(raw_pcb_ptr, wifi_netif);
        }
    } else {
        udp_pcb_ptr = udp_new();
        if (udp_pcb_ptr) {
            udp_bind(udp_pcb_ptr, &src_addr, 0);
        }
    }
    UNLOCK_TCPIP_CORE();

    if (!udp_pcb_ptr && !raw_pcb_ptr) {
        DBG("CSI", "TrafficGen: PCB alloc failed (icmp=%d)", useICMP);
        svc->_trafficGenRunning.store(false);
        vTaskDelete(NULL);
        return;
    }

    const uint32_t interval_us = 1000000 / svc->_trafficRatePps;
    int64_t next_send = esp_timer_get_time();
    uint16_t icmp_seq = 0;

    DBG("CSI", "TrafficGen task: netif=%c%c dst=" IPSTR " mode=%s port=%u",
                  wifi_netif->name[0], wifi_netif->name[1],
                  IP2STR((esp_ip4_addr_t*)&target_ip),
                  useICMP ? "ICMP" : "UDP", port);

    while (svc->_trafficGenRunning.load()) {
        err_t err = ERR_OK;

        if (useICMP) {
            // ICMP echo request (ping) — 8 byte header + 4 byte payload
            struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + 4, PBUF_RAM);
            if (p) {
                struct icmp_echo_hdr* echo = (struct icmp_echo_hdr*)p->payload;
                ICMPH_TYPE_SET(echo, ICMP_ECHO);
                ICMPH_CODE_SET(echo, 0);
                echo->id = PP_HTONS(0xC510);
                echo->seqno = PP_HTONS(icmp_seq++);
                // 4 bytes payload
                memset((uint8_t*)p->payload + sizeof(struct icmp_echo_hdr), 0xAA, 4);
                echo->chksum = 0;
                echo->chksum = inet_chksum(p->payload, p->tot_len);

                LOCK_TCPIP_CORE();
                err = raw_sendto_if_src(raw_pcb_ptr, p, &dst_addr, wifi_netif, &src_addr);
                UNLOCK_TCPIP_CORE();
                pbuf_free(p);
            }
        } else {
            // UDP mode — send DNS query to configurable port
            struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, sizeof(DNS_QUERY), PBUF_RAM);
            if (p) {
                memcpy(p->payload, DNS_QUERY, sizeof(DNS_QUERY));
                LOCK_TCPIP_CORE();
                err = udp_sendto_if_src(udp_pcb_ptr, p, &dst_addr, port, wifi_netif, &src_addr);
                UNLOCK_TCPIP_CORE();
                pbuf_free(p);
            }
        }

        if (err != ERR_OK) {
            static uint32_t errCount = 0;
            if (errCount++ < 5) {
                DBG("CSI", "TrafficGen: send err=%d (icmp=%d)", err, useICMP);
            }
        }

        next_send += interval_us;
        int64_t now = esp_timer_get_time();
        int64_t sleep_us = next_send - now;

        if (sleep_us > 0) {
            vTaskDelay(pdMS_TO_TICKS((sleep_us + 999) / 1000));
        } else if (sleep_us < -100000) {
            next_send = now;
        }
    }

    LOCK_TCPIP_CORE();
    if (udp_pcb_ptr) udp_remove(udp_pcb_ptr);
    if (raw_pcb_ptr) raw_remove(raw_pcb_ptr);
    UNLOCK_TCPIP_CORE();

    vTaskDelete(NULL);
}

// ============================================================================
// CSI Packet Processing
// ============================================================================

void CSIService::_processCSI(wifi_csi_info_t* info) {
    const int8_t* buf = info->buf;
    int len = info->len;

    // --- Packet validation & normalization (ported from ESPectre) ---

    // STBC doubled packets: collapse 256→128 by averaging pairs
    int8_t collapsed[HT20_CSI_LEN];
    if (len == HT20_CSI_LEN_DOUBLE) {
        for (int i = 0; i < HT20_CSI_LEN; i++) {
            collapsed[i] = (int8_t)(((int)buf[i] + (int)buf[i + HT20_CSI_LEN]) / 2);
        }
        buf = collapsed;
        len = HT20_CSI_LEN;
    }

    // Short HT20 packets (57 SC = 114 bytes): remap with left guard padding
    int8_t remapped[HT20_CSI_LEN];
    if (len == HT20_CSI_LEN_SHORT) {
        memset(remapped, 0, HT20_CSI_LEN);
        memcpy(remapped + HT20_SHORT_LEFT_PAD, buf, HT20_CSI_LEN_SHORT);
        buf = remapped;
        len = HT20_CSI_LEN;
    }

    // Validate: only accept standard HT20 length
    if (len != HT20_CSI_LEN) return;

    int totalSc = len / 2; // 64

    // Extract amplitudes + phases for selected subcarriers
    float amps[NUM_SUBCARRIERS];
    float phases[NUM_SUBCARRIERS];
    uint8_t numAmps = 0;

    for (int i = 0; i < NUM_SUBCARRIERS; i++) {
        int sc = SUBCARRIERS[i];
        if (sc >= totalSc) continue;

        // Espressif CSI format: [Imaginary, Real] per subcarrier
        float Q = static_cast<float>(buf[sc * 2]);
        float I = static_cast<float>(buf[sc * 2 + 1]);
        amps[numAmps] = sqrtf(I * I + Q * Q);
        phases[numAmps] = atan2f(Q, I);
        numAmps++;
    }

    if (numAmps < 2) return;

    // --- Spatial turbulence (two-pass variance, CV normalized) ---
    float ampSum = 0;
    for (uint8_t i = 0; i < numAmps; i++) ampSum += amps[i];
    float ampMean = ampSum / numAmps;

    float ampVar = 0;
    for (uint8_t i = 0; i < numAmps; i++) {
        float d = amps[i] - ampMean;
        ampVar += d * d;
    }
    ampVar /= numAmps;
    if (ampVar < 0) ampVar = 0;
    float rawStd = sqrtf(ampVar);

    // CV normalization: std/mean (gain-invariant for ESP32 without AGC lock)
    float turbulence = (ampMean > 0.0f) ? rawStd / ampMean : 0.0f;

    // --- Hampel outlier filter ---
    _hampelState.buffer[_hampelState.index] = turbulence;
    _hampelState.index = (_hampelState.index + 1) % HAMPEL_WINDOW;
    if (_hampelState.count < HAMPEL_WINDOW) _hampelState.count++;

    if (_hampelState.count >= 3) {
        float sorted[11];
        float deviations[11];
        uint8_t n = _hampelState.count;
        memcpy(sorted, _hampelState.buffer, n * sizeof(float));
        float median = calculateMedian(sorted, n);

        for (uint8_t i = 0; i < n; i++) {
            deviations[i] = fabsf(_hampelState.buffer[i] - median);
        }
        float mad = calculateMedian(deviations, n);

        float deviation = fabsf(turbulence - median);
        if (deviation > HAMPEL_THRESHOLD * MAD_SCALE * mad) {
            turbulence = median; // Replace outlier
        }
    }

    // --- Low-pass filter ---
    if (!_lowpassState.initialized) {
        _lowpassState.x_prev = turbulence;
        _lowpassState.y_prev = turbulence;
        _lowpassState.initialized = true;
    } else {
        float y = _lowpassState.b0 * turbulence + _lowpassState.b0 * _lowpassState.x_prev
                  - _lowpassState.a1 * _lowpassState.y_prev;
        _lowpassState.x_prev = turbulence;
        _lowpassState.y_prev = y;
        turbulence = y;
    }

    _lastTurbulence = turbulence;
    _lastAmpSum = ampSum;

    // --- Phase turbulence (std of inter-subcarrier phase diffs) ---
    if (numAmps > 2) {
        float pDiffs[NUM_SUBCARRIERS - 1];
        uint8_t nDiffs = 0;
        for (uint8_t i = 1; i < numAmps; i++) {
            pDiffs[nDiffs++] = phases[i] - phases[i - 1];
        }
        if (nDiffs > 1) {
            float pMean = 0;
            for (uint8_t i = 0; i < nDiffs; i++) pMean += pDiffs[i];
            pMean /= nDiffs;
            float pVar = 0;
            for (uint8_t i = 0; i < nDiffs; i++) {
                float d = pDiffs[i] - pMean;
                pVar += d * d;
            }
            pVar /= nDiffs;
            if (pVar < 0) pVar = 0;
            _lastPhaseTurb = sqrtf(pVar);
        }
    }

    // --- SA-WiSense ratio turbulence ---
    if (numAmps > 1) {
        float ratios[NUM_SUBCARRIERS - 1];
        uint8_t nRatios = 0;
        for (uint8_t i = 0; i + 1 < numAmps; i++) {
            if (amps[i + 1] > 0.1f) {
                ratios[nRatios++] = amps[i] / amps[i + 1];
            }
        }
        if (nRatios > 1) {
            float rMean = 0;
            for (uint8_t i = 0; i < nRatios; i++) rMean += ratios[i];
            rMean /= nRatios;
            float rVar = 0;
            for (uint8_t i = 0; i < nRatios; i++) {
                float d = ratios[i] - rMean;
                rVar += d * d;
            }
            rVar /= nRatios;
            if (rVar < 0) rVar = 0;
            _lastRatioTurb = sqrtf(rVar);
        }
    }

    // --- Breathing bandpass filter on amplitude sum ---
    if (!_breathFilter.initialized) {
        _breathFilter.hp_x_prev = ampSum;
        _breathFilter.initialized = true;
    } else {
        float hp = BREATH_HP_B0 * (ampSum - _breathFilter.hp_x_prev)
                    - BREATH_HP_A1 * _breathFilter.hp_y_prev;
        _breathFilter.hp_x_prev = ampSum;
        _breathFilter.hp_y_prev = hp;

        float lp = BREATH_LP_B0 * (hp + _breathFilter.lp_x_prev)
                    - BREATH_LP_A1 * _breathFilter.lp_y_prev;
        _breathFilter.lp_x_prev = hp;
        _breathFilter.lp_y_prev = lp;

        float sq = lp * lp;
        _breathFilter.energy = BREATH_ENERGY_ALPHA * sq
                               + (1.0f - BREATH_ENERGY_ALPHA) * _breathFilter.energy;
    }

    // --- Add to circular buffer (Welford's incremental variance) ---
    if (_turbBuffer) {
        float newVal = turbulence;
        if (_bufCount < _windowSize) {
            _turbBuffer[_bufIndex] = newVal;
            _bufCount++;
            float delta = newVal - _runningMean;
            _runningMean += delta / _bufCount;
            float delta2 = newVal - _runningMean;
            _runningM2 += delta * delta2;
        } else {
            float oldVal = _turbBuffer[_bufIndex];
            _turbBuffer[_bufIndex] = newVal;
            float newMean = _runningMean + (newVal - oldVal) / _windowSize;
            _runningM2 += (newVal - oldVal) * (newVal - newMean + oldVal - _runningMean);
            if (_runningM2 < 0) _runningM2 = 0;
            _runningMean = newMean;
        }
        _bufIndex = (_bufIndex + 1) % _windowSize;
        _runningVariance = (_bufCount >= _windowSize) ? _runningM2 / _windowSize : 0;
    }

    _totalPackets++;
    _windowPackets++;
}

// ============================================================================
// Motion State (temporal smoothing + hysteresis + breathing hold)
// ============================================================================

void CSIService::_updateMotionState() {
    if (_bufCount < _windowSize) return;

    bool rawMotion;
    if (!_motionState) {
        rawMotion = _runningVariance > _threshold;
    } else {
        rawMotion = _runningVariance >= _threshold * _hysteresis;
    }

    // N/M temporal smoothing (4/6 enter, 5/6 exit — matches ESPectre)
    _smoothHistory = ((_smoothHistory << 1) | (rawMotion ? 1 : 0)) & ((1 << SMOOTH_WINDOW) - 1);
    if (_smoothCount < SMOOTH_WINDOW) _smoothCount++;

    uint8_t motionCount = 0;
    uint8_t h = _smoothHistory;
    for (uint8_t i = 0; i < _smoothCount; i++) {
        motionCount += (h & 1);
        h >>= 1;
    }

    bool detectorMotion;
    if (!_motionState) {
        detectorMotion = (motionCount >= SMOOTH_ENTER && _smoothCount >= SMOOTH_ENTER);
    } else {
        uint8_t idleCount = _smoothCount - motionCount;
        detectorMotion = !(idleCount >= SMOOTH_EXIT && _smoothCount >= SMOOTH_EXIT);
    }

    // Breathing-aware presence hold (ported from ESPectre)
    // If detector says IDLE but breathing/phase suggest stationary person → hold MOTION
    if (!detectorMotion && _motionState && _idleInitialized) {
        float breathScore = getBreathingScore();
        bool breathHold = (breathScore > _idleMeanTurb * 2.0f) &&
                          (_lastPhaseTurb > _idleMeanPhase * 1.5f);

        if (breathHold && _breathHoldCount < BREATH_HOLD_MAX) {
            _breathHoldCount++;
            return; // Keep MOTION state, don't update
        }
    }

    if (detectorMotion) {
        _motionState = true;
        _breathHoldCount = 0;
    } else {
        _motionState = false;
        _breathHoldCount = 0;
    }

    // Update idle baselines when idle (EMA)
    if (!_motionState && _bufCount >= _windowSize) {
        float alpha = 1.0f / _windowSize;
        if (!_idleInitialized) {
            _idleMeanTurb = _lastTurbulence;
            _idleMeanPhase = _lastPhaseTurb;
            _idleAmpBaseline = _lastAmpSum;  // Real amplitude sum, not placeholder
            _idleInitialized = true;
        } else {
            _idleMeanTurb = alpha * _lastTurbulence + (1.0f - alpha) * _idleMeanTurb;
            _idleMeanPhase = alpha * _lastPhaseTurb + (1.0f - alpha) * _idleMeanPhase;
            _idleAmpBaseline = alpha * _lastAmpSum + (1.0f - alpha) * _idleAmpBaseline;
        }
    }
}

// ============================================================================
// Accessors
// ============================================================================

float CSIService::getBreathingScore() const {
    return _breathFilter.initialized ? sqrtf(_breathFilter.energy) : 0.0f;
}

float CSIService::getCompositeScore() const {
    if (!_idleInitialized) return 0.0f;

    float turbDev = 0;
    if (_idleMeanTurb > 1e-6f) {
        turbDev = (_runningMean - _idleMeanTurb) / _idleMeanTurb;
        if (turbDev < 0) turbDev = 0;
    }
    float phaseDev = 0;
    if (_idleMeanPhase > 1e-6f) {
        phaseDev = (_lastPhaseTurb - _idleMeanPhase) / _idleMeanPhase;
        if (phaseDev < 0) phaseDev = 0;
    }

    return 0.35f * turbDev + 0.25f * phaseDev +
           0.20f * _lastRatioTurb + 0.20f * getBreathingScore();
}

// ============================================================================
// MQTT Publishing
// ============================================================================

void CSIService::_publishMQTT() {
    if (!_mqtt || !_mqtt->connected()) return;

    char val[16];

    snprintf(val, sizeof(val), "%s", _motionState ? "ON" : "OFF");
    _mqtt->publish(_tMotion, val, true);

    snprintf(val, sizeof(val), "%.4f", _lastTurbulence);
    _mqtt->publish(_tTurbulence, val);

    snprintf(val, sizeof(val), "%.4f", _runningVariance);
    _mqtt->publish(_tVariance, val);

    snprintf(val, sizeof(val), "%.4f", _lastPhaseTurb);
    _mqtt->publish(_tPhaseTurb, val);

    snprintf(val, sizeof(val), "%.4f", _lastRatioTurb);
    _mqtt->publish(_tRatioTurb, val);

    snprintf(val, sizeof(val), "%.4f", getBreathingScore());
    _mqtt->publish(_tBreathing, val);

    snprintf(val, sizeof(val), "%.4f", getCompositeScore());
    _mqtt->publish(_tComposite, val);

    snprintf(val, sizeof(val), "%lu", (unsigned long)_totalPackets);
    _mqtt->publish(_tPackets, val);
}

// ============================================================================
// Main loop update
// ============================================================================

void CSIService::update() {
    if (!_active) return;

    // Force reconnect requested by user
    if (_reconnectRequested) {
        _reconnectRequested = false;
        _stopTrafficGen();
        WiFi.reconnect();
        Serial.println("[CSI] Forced WiFi reconnect");
    }

    // Reconnect WiFi if dropped
    if (WiFi.status() != WL_CONNECTED) {
        // Stop traffic gen when WiFi is down
        if (_trafficGenRunning.load()) _stopTrafficGen();

        static uint32_t lastReconnect = 0;
        if (millis() - lastReconnect > 10000) {
            WiFi.reconnect();
            lastReconnect = millis();
        }
        return;
    }

    // Restart traffic gen after WiFi reconnect
    if (!_trafficGenRunning.load() && WiFi.status() == WL_CONNECTED) {
        static uint32_t lastAttempt = 0;
        if (millis() - lastAttempt > 5000) {
            lastAttempt = millis();
            DBG("CSI", "TrafficGen retry: WiFi=%d IP=%s",
                WiFi.status(), WiFi.localIP().toString().c_str());
            _startTrafficGen();
        }
    }

    // Periodic CSI diagnostics (every 30s)
    static uint32_t lastDiag = 0;
    if (millis() - lastDiag > 30000) {
        lastDiag = millis();
        DBG("CSI", "diag: pps=%.1f pkts=%lu tgen=%d wifi=%d ip=%s rssi=%d",
            _packetRate, (unsigned long)_totalPackets,
            _trafficGenRunning.load() ? 1 : 0,
            WiFi.status(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    uint32_t now = millis();
    if (now - _lastPublishMs >= _publishIntervalMs) {
        uint32_t windowMs = now - _lastPublishMs;
        if (windowMs > 0) _packetRate = (float)_windowPackets * 1000.0f / (float)windowMs;
        _lastPublishMs = now;
        _updateMotionState();

        // Calibration sample collection
        if (_calibrating && _bufCount >= _windowSize) {
            _calibVarSum += _runningVariance;
            _calibSamples++;
            if (now - _calibStartMs >= _calibDurationMs) {
                if (_calibSamples > 0) {
                    float mean = _calibVarSum / _calibSamples;
                    float newThr = mean * 1.5f;
                    if (newThr < 0.001f) newThr = 0.001f;
                    _threshold = newThr;
                    Serial.printf("[CSI] Calibration done: %u samples, mean=%.4f, threshold=%.4f\n",
                                  _calibSamples, mean, _threshold);
                }
                _calibrating = false;
            }
        }

        _publishMQTT();
        _windowPackets = 0;
    }
}

// ============================================================================
// Diagnostics / control
// ============================================================================

int CSIService::getWifiRSSI() const {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
}

String CSIService::getWifiSSID() const {
    return (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
}

void CSIService::resetIdleBaseline() {
    _idleInitialized = false;
    _idleMeanTurb = 0;
    _idleMeanPhase = 0;
    _idleAmpBaseline = 0;
    _bufCount = 0;
    _bufIndex = 0;
    _runningMean = 0;
    _runningM2 = 0;
    _runningVariance = 0;
    _smoothHistory = 0;
    _smoothCount = 0;
    _motionState = false;
    _breathHoldCount = 0;
    _hampelState.index = 0;
    _hampelState.count = 0;
    _lowpassState.initialized = false;
    if (_turbBuffer) memset(_turbBuffer, 0, _windowSize * sizeof(float));
    Serial.println("[CSI] Idle baseline reset — recollecting samples");
}

void CSIService::forceReconnect() {
    _reconnectRequested = true;
}

void CSIService::calibrateThreshold(uint32_t durationMs) {
    if (durationMs < 1000) durationMs = 1000;
    if (durationMs > 60000) durationMs = 60000;
    _calibrating = true;
    _calibStartMs = millis();
    _calibDurationMs = durationMs;
    _calibVarSum = 0.0f;
    _calibSamples = 0;
    Serial.printf("[CSI] Calibration started — sampling %u ms (keep area still)\n", durationMs);
}

float CSIService::getCalibrationProgress() const {
    if (!_calibrating || _calibDurationMs == 0) return 0.0f;
    uint32_t elapsed = millis() - _calibStartMs;
    if (elapsed >= _calibDurationMs) return 1.0f;
    return (float)elapsed / (float)_calibDurationMs;
}
