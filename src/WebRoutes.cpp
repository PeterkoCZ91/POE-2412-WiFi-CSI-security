#ifndef LITE_BUILD
#include "WebRoutes.h"
#include <esp_task_wdt.h>
#include "ConfigManager.h"
#include <ETH.h>

// Radar OUT pin - must match main.cpp definition
#ifndef RADAR_OUT_PIN
#define RADAR_OUT_PIN -1
#endif
#include "services/LD2412Service.h"
#include "services/MQTTService.h"
#include "services/SecurityMonitor.h"
#include "services/TelegramService.h"
#include "services/NotificationService.h"
#include "services/LogService.h"
#include "services/EventLog.h"
#include "services/ConfigSnapshot.h"
#ifdef USE_CSI
#include "services/CSIService.h"
#endif
#include <LittleFS.h>
#include "services/BluetoothService.h"
#include "debug.h"
#include <ArduinoJson.h>
#include <Update.h>
#include "constants.h"
#include "web_interface.h"
#include <esp_ota_ops.h>

namespace WebRoutes {

// Static copy of dependencies - persists after setup() returns
static Dependencies _deps;

// Authentication helper implementation
bool checkAuth(AsyncWebServerRequest *request) {
    if (_deps.config == nullptr) return false;
    if (!request->authenticate(_deps.config->auth_user, _deps.config->auth_pass)) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

void setup(Dependencies& deps) {
    // Copy all pointers to static storage
    _deps = deps;

    DBG("WEB", "Setting up web routes...");

    // Root handler — serve from LittleFS if available, otherwise fall back to PROGMEM
    _deps.server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (LittleFS.exists("/index.html.gz")) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("X-Asset-Source", "littlefs");
            request->send(response);
        } else {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (const uint8_t*)index_html, strlen(index_html));
            response->addHeader("X-Asset-Source", "progmem");
            request->send(response);
        }
    });

    setupTelemetryRoutes();
    setupConfigRoutes();
    setupSecurityRoutes();
    setupSystemRoutes();
    setupAlarmRoutes();
    setupLogRoutes();
    setupSnapshotRoutes();
    setupWwwRoutes();
    setupCSIRoutes();

    DBG("WEB", "Web routes initialized");
}

void setupTelemetryRoutes() {
    _deps.server->on("/api/telemetry", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.radar->getTelemetryJson(doc);
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["connected"] = _deps.radar->isRadarConnected();
        #if RADAR_OUT_PIN >= 0
        doc["out_pin"] = digitalRead(RADAR_OUT_PIN);
        #endif
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["uptime"] = millis() / 1000;
        doc["fw_version"] = _deps.fwVersion;
        doc["resolution"] = _deps.config->radar_resolution;
        doc["is_default_pass"] = (String(_deps.config->auth_user) == "admin" && String(_deps.config->auth_pass) == "admin");
        doc["hostname"] = String(_deps.config->hostname);
        doc["reset_history"] = _deps.preferences->getString("reset_history", "[]");

        // Extended Health Stats (REQ-002)
        doc["uart_state"] = _deps.radar->getUARTStateString();
        doc["frame_rate"] = _deps.radar->getFrameRate();
        doc["error_count"] = _deps.radar->getErrorCount();
        doc["health_score"] = _deps.radar->getHealthScore();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_heap"] = ESP.getMinFreeHeap();
        doc["chip_temp"] = temperatureRead();

        // OTA rollback state
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            doc["ota_state"] = (ota_state == ESP_OTA_IMG_PENDING_VERIFY) ? "pending_verify" :
                               (ota_state == ESP_OTA_IMG_VALID)          ? "valid" :
                               (ota_state == ESP_OTA_IMG_INVALID)        ? "invalid" : "unknown";
        }

        JsonObject build = doc["build"].to<JsonObject>();
        build["rx_pin"]  = RADAR_RX_PIN;
        build["tx_pin"]  = RADAR_TX_PIN;
        build["out_pin"] = RADAR_OUT_PIN;

        JsonObject chip = doc["chip"].to<JsonObject>();
        chip["model"]      = ESP.getChipModel();
        chip["revision"]   = ESP.getChipRevision();
        chip["cores"]      = ESP.getChipCores();
        chip["mac"]        = ETH.macAddress();
        chip["flash_size"] = ESP.getFlashChipSize();

        JsonObject eth = doc["ethernet"].to<JsonObject>();
        eth["link_up"] = ETH.linkUp();
        eth["ip"] = ETH.localIP().toString();
        eth["mac"] = ETH.macAddress();
        eth["speed"] = ETH.linkSpeed();

        JsonObject mqtt = doc["mqtt"].to<JsonObject>();
        mqtt["enabled"] = _deps.config->mqtt_enabled;
        mqtt["connected"] = _deps.mqttService->connected();
        mqtt["server"] = _deps.config->mqtt_server;
        mqtt["port"] = _deps.config->mqtt_port;
        mqtt["user"] = strlen(_deps.config->mqtt_user) > 0 ? "***" : "";
        mqtt["id"] = _deps.config->mqtt_id;
        mqtt["tls"] = (String(_deps.config->mqtt_port) == "8883");

        // CSI status (compiled flag + runtime active flag)
        JsonObject csi = doc["csi"].to<JsonObject>();
        #ifdef USE_CSI
        csi["compiled"] = true;
        csi["enabled"]  = _deps.config->csi_enabled;
        csi["active"]   = (_deps.csiService != nullptr) && _deps.csiService->isActive();
        #else
        csi["compiled"] = false;
        csi["enabled"]  = false;
        csi["active"]   = false;
        #endif

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/radar/learn-static", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        uint16_t dur = 180;
        if (request->hasParam("duration")) dur = request->getParam("duration")->value().toInt();
        dur = constrain(dur, 30, 28800);
        if (_deps.radar->startStaticLearn(dur)) {
            request->send(200, "text/plain", "Learn started");
        } else {
            request->send(409, "text/plain", "Already running");
        }
    });

    _deps.server->on("/api/radar/learn-static", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.radar->getLearnResultJson(doc);
        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // Auto-create ignore_static_only zone from learn results
    _deps.server->on("/api/radar/apply-learn", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        // Get learn results
        JsonDocument learnDoc;
        _deps.radar->getLearnResultJson(learnDoc);

        if (!(learnDoc["suggest_ready"] | false)) {
            request->send(409, "text/plain", "No valid learn data — run /learn first");
            return;
        }

        int minCm = learnDoc["suggest_min_cm"] | 0;
        int maxCm = learnDoc["suggest_max_cm"] | 0;
        int topGate = learnDoc["top_gate"] | 0;

        // Optional custom name from query param
        String zoneName = "refl_g" + String(topGate);
        if (request->hasParam("name")) {
            zoneName = request->getParam("name")->value();
            zoneName = zoneName.substring(0, 15); // fit AlertZone.name[16]
        }

        // Parse existing zones
        JsonDocument zonesDoc;
        String currentZones;
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            currentZones = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        } else {
            currentZones = "[]";
        }
        deserializeJson(zonesDoc, currentZones);
        JsonArray arr = zonesDoc.to<JsonArray>();

        // Check for overlapping zone
        for (JsonObject z : arr) {
            int zMin = z["min"] | 0;
            int zMax = z["max"] | 0;
            if (minCm <= zMax && maxCm >= zMin) {
                request->send(409, "text/plain",
                    "Overlaps with existing zone '" + String((const char*)(z["name"] | "?")) + "' (" + String(zMin) + "-" + String(zMax) + "cm)");
                return;
            }
        }

        // Append new ignore_static_only zone
        JsonObject newZone = arr.add<JsonObject>();
        newZone["name"] = zoneName;
        newZone["min"] = minCm;
        newZone["max"] = maxCm;
        newZone["level"] = 0;
        newZone["delay"] = 0;
        newZone["enabled"] = true;
        newZone["alarm_behavior"] = 3;  // ignore_static_only

        String newJson;
        serializeJson(zonesDoc, newJson);

        // Schedule zones update (same mechanism as POST /api/zones)
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            *_deps.pendingZonesJson = newJson;
            *_deps.pendingZonesUpdate = true;
            xSemaphoreGive(*_deps.zonesMutex);
        }

        DBG("WEB", "Auto-zone from learn: %s %d-%dcm (behavior=3)", zoneName.c_str(), minCm, maxCm);

        JsonDocument respDoc;
        respDoc["status"] = "created";
        respDoc["zone_name"] = zoneName;
        respDoc["min_cm"] = minCm;
        respDoc["max_cm"] = maxCm;
        respDoc["alarm_behavior"] = 3;
        String resp;
        serializeJson(respDoc, resp);
        request->send(200, "application/json", resp);
    });
}

void setupConfigRoutes() {
    // --- Global Config ---
    _deps.server->on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;

        doc["min_gate"] = _deps.radar->getMinGate();
        doc["max_gate"] = _deps.radar->getMaxGate();
        doc["duration"] = _deps.radar->getMaxGateDuration();
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["resolution"] = _deps.config->radar_resolution;
        doc["led_start"] = _deps.config->startup_led_sec;
        doc["led_en"] = _deps.config->led_enabled;
        doc["chip_temp_interval"] = _deps.config->chip_temp_interval;
        doc["eng_mode"] = _deps.radar->isEngineeringMode();

        JsonArray mov = doc["mov_sens"].to<JsonArray>();
        JsonArray stat = doc["stat_sens"].to<JsonArray>();

        const uint8_t* m = _deps.radar->getMotionSensitivityArray();
        const uint8_t* s = _deps.radar->getStaticSensitivityArray();

        for(int i=0; i<14; i++) {
            mov.add(m[i]);
            stat.add(s[i]);
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        bool changed = false;
        bool needsReboot = false;

        if (request->hasParam("min_gate") && request->hasParam("gate")) {
             int min_gate = request->getParam("min_gate")->value().toInt();
             int max_gate = request->getParam("gate")->value().toInt();

             if (min_gate >= 0 && min_gate <= 13 && max_gate >= 1 && max_gate <= 13 && min_gate <= max_gate) {
                 _deps.radar->setParamConfig((uint8_t)min_gate, (uint8_t)max_gate, 10);
                 _deps.preferences->putUInt("radar_min", min_gate);
                 _deps.preferences->putUInt("radar_max", max_gate);
                 changed = true;
             }
        }

        if (request->hasParam("hold")) {
            unsigned long hold = request->getParam("hold")->value().toInt();
            if (hold >= 1 && hold <= 300000) {
                _deps.radar->setHoldTime(hold);
                _deps.preferences->putULong("hold_time", hold);
                changed = true;
            }
        }

        if (request->hasParam("mov")) {
            int mov = request->getParam("mov")->value().toInt();
            _deps.radar->setMotionSensitivity((uint8_t)mov);
            changed = true;
        }

        if (request->hasParam("led_en")) {
            bool en = request->getParam("led_en")->value() == "1";
            _deps.config->led_enabled = en;
            _deps.preferences->putBool("led_en", en);
            changed = true;
        }

        if (request->hasParam("chip_temp_interval")) {
            int intv = request->getParam("chip_temp_interval")->value().toInt();
            if (intv >= 0 && intv <= 86400) {
                _deps.config->chip_temp_interval = (uint16_t)intv;
                _deps.preferences->putUShort("temp_intv", (uint16_t)intv);
                changed = true;
            }
        }

        if (request->hasParam("hostname")) {
            String hn = request->getParam("hostname")->value();
            if (hn.length() > 0 && hn.length() < 32) {
                strncpy(_deps.config->hostname, hn.c_str(), sizeof(_deps.config->hostname)-1);
                _deps.preferences->putString("hostname", hn);
                changed = true;
                needsReboot = true;
            }
        }

        if (request->hasParam("resolution")) {
            float res = request->getParam("resolution")->value().toFloat();
            if (res >= 0.19f && res <= 0.21f) res = 0.20f;
            else if (res >= 0.49f && res <= 0.51f) res = 0.50f;
            else if (res >= 0.74f && res <= 0.76f) res = 0.75f;
            if (res == 0.20f || res == 0.50f || res == 0.75f) {
               _deps.config->radar_resolution = res;
               _deps.preferences->putFloat("radar_res", res);
               _deps.radar->setResolution(res);  // Send cmd 0x01 to radar
               changed = true;
               needsReboot = true;
            }
        }

        if (changed) {
            request->send(200, "text/plain", needsReboot ? "Config saved, rebooting..." : "Config saved");
            if (needsReboot) *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "No valid parameters provided");
        }
    });

    // /api/wifi/config removed — POE board has no WiFi

    // --- MQTT Config ---
    _deps.server->on("/api/mqtt/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        bool en = true;
        if (request->hasParam("enabled")) {
             en = (request->getParam("enabled")->value() == "1");
             _deps.preferences->putBool("mqtt_en", en);
             _deps.config->mqtt_enabled = en;
        }

        if (request->hasParam("server")) {
            String s = request->getParam("server")->value();
            _deps.preferences->putString("mqtt_server", s);
            s.toCharArray(_deps.config->mqtt_server, 60);

            if (request->hasParam("port")) {
                String p = request->getParam("port")->value();
                _deps.preferences->putString("mqtt_port", p);
                p.toCharArray(_deps.config->mqtt_port, 6);
            }
            if (request->hasParam("user")) {
                String u = request->getParam("user")->value();
                if (u != "***") { _deps.preferences->putString("mqtt_user", u); u.toCharArray(_deps.config->mqtt_user, 40); }
            }
            if (request->hasParam("pass")) {
                String pw = request->getParam("pass")->value();
                if (pw != "***") { _deps.preferences->putString("mqtt_pass", pw); pw.toCharArray(_deps.config->mqtt_pass, 40); }
            }

            String id = request->hasParam("id") ? request->getParam("id")->value() : String(_deps.config->mqtt_id);
            _deps.preferences->putString("mqtt_id", id);
            id.toCharArray(_deps.config->mqtt_id, 40);
        }

        request->send(200, "text/plain", "Saved");
        *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/preset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("name")) {
            String name = request->getParam("name")->value();
            DBG("WEB", "Applying preset: %s", name.c_str());
            
            if (name == "indoor") {
                _deps.radar->setMotionSensitivity(50);
                _deps.radar->setStaticSensitivity(40);
                _deps.securityMonitor->setPetImmunity(5);
                _deps.preferences->putUInt("sec_pet", 5);
            } 
            else if (name == "outdoor") {
                uint8_t m[14] = {20,30,40,40,40,40,40,40,40,40,40,30,20,10};
                _deps.radar->setMotionSensitivity(m);
                _deps.radar->setStaticSensitivity(20);
                _deps.securityMonitor->setPetImmunity(15);
                _deps.preferences->putUInt("sec_pet", 15);
            }
            else if (name == "pet") {
                uint8_t m[14] = {10,10,15,30,45,50,50,50,50,50,50,50,50,50};
                _deps.radar->setMotionSensitivity(m);
                _deps.radar->setStaticSensitivity(25);
                _deps.securityMonitor->setPetImmunity(25);
                _deps.preferences->putUInt("sec_pet", 25);
            }
            
            request->send(200, "text/plain", "Preset applied");
        } else {
            request->send(400, "text/plain", "Missing name");
        }
    });

    // --- Telegram Config ---
    _deps.server->on("/api/telegram/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["enabled"] = _deps.telegramBot->isEnabled();
        doc["token"] = strlen(_deps.telegramBot->getToken()) > 0 ? "***" : "";
        doc["chat_id"] = strlen(_deps.telegramBot->getChatId()) > 0 ? "***" : "";

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/telegram/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        auto getP = [&](const char* n) -> String {
            if (request->hasParam(n, true)) return request->getParam(n, true)->value();
            if (request->hasParam(n))       return request->getParam(n)->value();
            return "";
        };
        String token = getP("token");
        String chat  = getP("chat_id");
        bool enabled = getP("enabled") == "1";

        if (token.length() > 0 && token != "***") {
            _deps.telegramBot->setToken(token.c_str());
        }
        if (chat.length() > 0 && chat != "***") {
            _deps.telegramBot->setChatId(chat.c_str());
        }
        _deps.notificationService->setTelegramConfig(
            _deps.telegramBot->getToken(), _deps.telegramBot->getChatId());
        _deps.telegramBot->setEnabled(enabled);

        request->send(200, "text/plain", "Telegram config saved");
        if (enabled) *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/telegram/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        String testMsg = "🔔 *Test Notifikace*\n";
        testMsg += "📡 " + String(_deps.config->mqtt_id) + "\n";
        testMsg += "🌐 " + ETH.localIP().toString() + "\n";
        testMsg += "🏷️ FW: " + String(_deps.fwVersion);
        bool res = _deps.telegramBot->sendMessageDirect(testMsg);
        JsonDocument resDoc;
        resDoc["success"] = res;
        if (!res) resDoc["error"] = "Send failed (DNS/TCP/TLS/token/chat_id)";
        String response;
        serializeJson(resDoc, response);
        request->send(200, "application/json", response);
    });

    // --- Auth Config ---
    _deps.server->on("/api/auth/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("user") && request->hasParam("pass")) {
            String user = request->getParam("user")->value();
            String pass = request->getParam("pass")->value();
            if (user.length() >= 4 && pass.length() >= 4) {
                strncpy(_deps.config->auth_user, user.c_str(), sizeof(_deps.config->auth_user)-1);
                strncpy(_deps.config->auth_pass, pass.c_str(), sizeof(_deps.config->auth_pass)-1);
                _deps.preferences->putString("auth_user", user);
                _deps.preferences->putString("auth_pass", pass);
                request->send(200, "text/plain", "Credentials changed, rebooting...");
                *_deps.shouldReboot = true;
            } else {
                request->send(400, "text/plain", "Too short (min 4 chars)");
            }
        } else {
            request->send(400, "text/plain", "Missing params");
        }
    });

    // --- Zones ---
    _deps.server->on("/api/zones", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        String copy;
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        } else {
            copy = "[]";
        }
        request->send(200, "application/json", copy);
    });

    _deps.server->on("/api/zones", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* bodyData = (char*)request->_tempObject;
        if (bodyData) {
            if (strlen(bodyData) > 0) {
                if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    *_deps.pendingZonesJson = String(bodyData);
                    *_deps.pendingZonesUpdate = true;
                    xSemaphoreGive(*_deps.zonesMutex);
                }
            }
            free(bodyData);
            request->_tempObject = nullptr;
        }

        request->send(200, "text/plain", "Zones received");
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 4096) return;  // Limit body size
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });

    // --- Config Export/Import ---
    _deps.server->on("/api/config/export", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["mqtt_server"] = String(_deps.config->mqtt_server);
        doc["mqtt_port"] = String(_deps.config->mqtt_port);
        doc["mqtt_user"] = strlen(_deps.config->mqtt_user) > 0 ? "***" : "";
        doc["mqtt_pass"] = strlen(_deps.config->mqtt_pass) > 0 ? "***" : "";
        doc["mqtt_id"] = String(_deps.config->mqtt_id);
        doc["hostname"] = String(_deps.config->hostname);
        doc["auth_user"] = "***";
        doc["auth_pass"] = "***";
        doc["radar_res"] = _deps.config->radar_resolution;
        doc["led_start"] = _deps.config->startup_led_sec;
        doc["hold_time"] = _deps.radar->getHoldTime();
        doc["pet_immunity"] = _deps.radar->getMinMoveEnergy();
        if (_deps.zonesMutex && xSemaphoreTake(*_deps.zonesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            doc["zones"] = *_deps.zonesJson;
            xSemaphoreGive(*_deps.zonesMutex);
        }
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/config/import", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* importData = (char*)request->_tempObject;
        if (importData) {
            if (strlen(importData) > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, importData);
                if (err) {
                    free(importData);
                    request->_tempObject = nullptr;
                    request->send(400, "text/plain", "Invalid JSON");
                    return;
                }

                if (doc["mqtt_server"].is<String>()) _deps.preferences->putString("mqtt_server", doc["mqtt_server"].as<String>());
                if (doc["mqtt_port"].is<String>()) _deps.preferences->putString("mqtt_port", doc["mqtt_port"].as<String>());
                if (doc["mqtt_user"].is<String>() && doc["mqtt_user"].as<String>() != "***") _deps.preferences->putString("mqtt_user", doc["mqtt_user"].as<String>());
                if (doc["mqtt_pass"].is<String>() && doc["mqtt_pass"].as<String>() != "***") _deps.preferences->putString("mqtt_pass", doc["mqtt_pass"].as<String>());
                if (doc["mqtt_id"].is<String>()) _deps.preferences->putString("mqtt_id", doc["mqtt_id"].as<String>());
                if (doc["auth_user"].is<String>() && doc["auth_user"].as<String>() != "***") _deps.preferences->putString("auth_user", doc["auth_user"].as<String>());
                if (doc["auth_pass"].is<String>() && doc["auth_pass"].as<String>() != "***") _deps.preferences->putString("auth_pass", doc["auth_pass"].as<String>());
                // bk_ssid/bk_pass removed — POE board has no WiFi
                if (doc["radar_res"].is<float>()) _deps.preferences->putFloat("radar_res", doc["radar_res"].as<float>());
                if (doc["led_start"].is<uint16_t>()) _deps.preferences->putUInt("led_start", doc["led_start"].as<uint16_t>());
                if (doc["hold_time"].is<unsigned long>()) _deps.preferences->putULong("hold_time", doc["hold_time"].as<unsigned long>());
                if (doc["zones"].is<String>()) _deps.preferences->putString("zones_json", doc["zones"].as<String>());
            }

            free(importData);
            request->_tempObject = nullptr;
        }

        request->send(200, "text/plain", "Config received, applying and rebooting...");
        *_deps.shouldReboot = true;
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 4096) return;  // Limit body size
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });
}

void setupSecurityRoutes() {
    _deps.server->on("/api/security/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["antimask_time"] = _deps.securityMonitor->getAntiMaskTime() / 1000;
        doc["antimask_enabled"] = _deps.securityMonitor->isAntiMaskEnabled();
        doc["loiter_time"] = _deps.securityMonitor->getLoiterTime() / 1000;
        doc["loiter_alert"] = _deps.securityMonitor->isLoiterAlertEnabled();
        doc["heartbeat"] = _deps.securityMonitor->getHeartbeatInterval() / INTERVAL_TELEMETRY_IDLE_MS;
        doc["pet_immunity"] = _deps.radar->getMinMoveEnergy();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/security/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        if (request->hasParam("antimask")) {
            unsigned long val = request->getParam("antimask")->value().toInt() * 1000;
            _deps.securityMonitor->setAntiMaskTime(val);
            _deps.preferences->putULong("sec_antimask", val);
        }
        if (request->hasParam("antimask_en")) {
            bool en = (request->getParam("antimask_en")->value() == "1");
            _deps.securityMonitor->setAntiMaskEnabled(en);
            _deps.preferences->putBool("sec_am_en", en);
        }
        if (request->hasParam("loiter")) {
            unsigned long val = request->getParam("loiter")->value().toInt() * 1000;
            _deps.securityMonitor->setLoiterTime(val);
            _deps.preferences->putULong("sec_loiter", val);
        }
        if (request->hasParam("loiter_alert")) {
            bool en = (request->getParam("loiter_alert")->value() == "1");
            _deps.securityMonitor->setLoiterAlertEnabled(en);
            _deps.preferences->putBool("sec_loit_en", en);
        }
        if (request->hasParam("heartbeat")) {
            unsigned long val = request->getParam("heartbeat")->value().toInt() * INTERVAL_TELEMETRY_IDLE_MS;
            _deps.securityMonitor->setHeartbeatInterval(val);
            _deps.preferences->putULong("sec_hb", val);
        }
        if (request->hasParam("pet")) {
            int val = request->getParam("pet")->value().toInt();
            if(val >= 0 && val <= 100) {
                 _deps.radar->setMinMoveEnergy((uint8_t)val);
                 _deps.securityMonitor->setPetImmunity((uint8_t)val);
                 _deps.preferences->putUInt("sec_pet", (uint8_t)val);
            }
        }
        request->send(200, "text/plain", "Security config saved");
    });
}

void setupSystemRoutes() {
    _deps.server->on("/api/update", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool success = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", success ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if (success) {
            DBG("OTA", "Update finished, delaying 500ms for response flush before reboot...");
            delay(500);  // Let HTTP response pass through nginx proxy before reboot
            *_deps.shouldReboot = true;
        }
    }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // Auth check on first chunk only — Digest auth re-validation on subsequent
        // chunks causes connection drop on ETH (LAN8720A) after 64KB buffer.
        // Use static flag to track auth state across chunks of same upload.
        static bool otaAuthorized = false;
        if (!index) {
            otaAuthorized = false;
            if (!request->authenticate(_deps.config->auth_user, _deps.config->auth_pass)) {
                DBG("OTA", "Upload auth failed");
                request->requestAuthentication();
                return;
            }
            otaAuthorized = true;
            DBG("OTA", "Update start: %s (%u bytes)", filename.c_str(), request->contentLength());
            // Save config snapshot before flash starts
            if (_deps.configSnapshot && _deps.preferences) {
                _deps.configSnapshot->saveSnapshot(_deps.preferences, _deps.fwVersion, "ota_http");
            }
            // Use content length if available, otherwise UPDATE_SIZE_UNKNOWN
            size_t updateSize = (request->contentLength() > 0) ? request->contentLength() : UPDATE_SIZE_UNKNOWN;
            if (!Update.begin(updateSize)) {
                Update.printError(Serial);
            }
        }
        if (!otaAuthorized) return;  // Skip data from unauthenticated request
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            esp_task_wdt_reset(); // keep WDT alive during long OTA transfers
        }
        if (final) {
            if (!Update.hasError() && Update.end(true)) {
                DBG("OTA", "Update success: %u B — reboot scheduled", index + len);
                *_deps.shouldReboot = true;  // Reboot even if response never sends (nginx proxy 502)
            } else {
                Update.printError(Serial);
                Update.abort();
            }
            otaAuthorized = false;  // Reset auth state for next upload
        }
    });

    // Debug log ring buffer — remote access to last 4KB of DBG() output
    _deps.server->on("/api/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "text/plain", DebugLog::instance().read());
    });
    _deps.server->on("/api/debug", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        DebugLog::instance().clear();
        request->send(200, "text/plain", "cleared");
    });

    _deps.server->on("/api/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "text/plain", "Rebooting...");
        *_deps.shouldReboot = true;
    });

    _deps.server->on("/api/radar/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.radar->restartRadar();
        request->send(200, "text/plain", "Radar restart command sent");
    });

    _deps.server->on("/api/radar/factory_reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.radar->factoryReset()) {
            request->send(200, "text/plain", "Radar factory reset OK");
        } else {
            request->send(500, "text/plain", "Radar factory reset failed");
        }
    });

    _deps.server->on("/api/radar/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.radar->startCalibration();
        request->send(200, "text/plain", "Calibration started (60s)");
    });

    _deps.server->on("/api/engineering", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("enable")) {
            bool en = request->getParam("enable")->value() == "1";
            _deps.radar->setEngineeringMode(en);
            request->send(200, "text/plain", en ? "Engineering mode ON" : "Engineering mode OFF");
        } else {
            request->send(400, "text/plain", "Missing enable param");
        }
    });

    // Per-gate sensitivity endpoint (CR-005)
    _deps.server->on("/api/radar/gate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        if (request->hasParam("gate") && request->hasParam("mov") && request->hasParam("stat")) {
            int gate = request->getParam("gate")->value().toInt();
            int mov = request->getParam("mov")->value().toInt();
            int stat = request->getParam("stat")->value().toInt();

            if (gate >= 0 && gate <= 13 && mov >= 0 && mov <= 100 && stat >= 0 && stat <= 100) {
                // Get current values and modify just one gate
                const uint8_t* currentMov = _deps.radar->getMotionSensitivityArray();
                const uint8_t* currentStat = _deps.radar->getStaticSensitivityArray();

                uint8_t movArr[14], statArr[14];
                memcpy(movArr, currentMov, 14);
                memcpy(statArr, currentStat, 14);

                movArr[gate] = (uint8_t)mov;
                statArr[gate] = (uint8_t)stat;

                bool movOk = _deps.radar->setMotionSensitivity(movArr);
                bool statOk = _deps.radar->setStaticSensitivity(statArr);

                if (movOk && statOk) {
                    request->send(200, "text/plain", "Gate sensitivity updated");
                } else {
                    request->send(500, "text/plain", "Radar command failed");
                }
            } else {
                request->send(400, "text/plain", "Invalid values (gate 0-13, sens 0-100)");
            }
        } else {
            request->send(400, "text/plain", "Missing params: gate, mov, stat");
        }
    });

    // Batch gate sensitivity endpoint — single JSON POST for all 14 gates
    _deps.server->on("/api/radar/gates", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;

        char* bodyData = (char*)request->_tempObject;
        if (!bodyData || strlen(bodyData) == 0) {
            request->send(400, "text/plain", "Empty body");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, bodyData);
        free(bodyData);
        request->_tempObject = nullptr;

        if (err) {
            request->send(400, "text/plain", "Invalid JSON");
            return;
        }

        JsonArray movArr = doc["mov"];
        JsonArray statArr = doc["stat"];

        if (movArr.size() != 14 || statArr.size() != 14) {
            request->send(400, "text/plain", "Need mov[14] and stat[14]");
            return;
        }

        uint8_t mov[14], stat[14];
        for (int i = 0; i < 14; i++) {
            int m = movArr[i].as<int>();
            int s = statArr[i].as<int>();
            if (m < 0 || m > 100 || s < 0 || s > 100) {
                request->send(400, "text/plain", "Values must be 0-100");
                return;
            }
            mov[i] = (uint8_t)m;
            stat[i] = (uint8_t)s;
        }

        bool movOk = _deps.radar->setMotionSensitivity(mov);
        bool statOk = _deps.radar->setStaticSensitivity(stat);

        if (movOk && statOk) {
            request->send(200, "text/plain", "Gates saved");
        } else {
            request->send(500, "text/plain", "Radar command failed");
        }
    }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!checkAuth(request)) return;

        if (index == 0) {
            if (total > 512) return;
            request->_tempObject = malloc(total + 1);
            if (request->_tempObject) ((char*)request->_tempObject)[0] = '\0';
        }

        char* buf = (char*)request->_tempObject;
        if (buf && index + len <= total) {
            memcpy(buf + index, data, len);
            buf[index + len] = '\0';
        }
    });

    _deps.server->on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request) {
        // No auth for version (GUI needs it for the header before auth)
        // Or keep it simple if GUI already handles auth
        request->send(200, "text/plain", _deps.fwVersion);
    });

    // Query Resolution endpoint (Task #12)
    _deps.server->on("/api/radar/resolution", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        int mode = _deps.radar->getResolution();
        doc["mode"] = mode;
        // Convert to float for display
        float res = 0.75f;
        if (mode == 1) res = 0.50f;
        else if (mode == 2) res = 0.20f;
        doc["resolution"] = res;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // Light Function/Threshold endpoints (Task #11)
    _deps.server->on("/api/radar/light", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["function"] = _deps.radar->getLightFunction();
        doc["threshold"] = _deps.radar->getLightThreshold();
        doc["current_level"] = _deps.radar->getLightLevel();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/radar/light", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;

        if (request->hasParam("function")) {
            int func = request->getParam("function")->value().toInt();
            if (func >= 0 && func <= 2) {
                if (_deps.radar->setLightFunction((uint8_t)func)) {
                    _deps.preferences->putUChar("light_func", (uint8_t)func);
                    changed = true;
                }
            }
        }
        if (request->hasParam("threshold")) {
            int thresh = request->getParam("threshold")->value().toInt();
            if (thresh >= 0 && thresh <= 255) {
                if (_deps.radar->setLightThreshold((uint8_t)thresh)) {
                    _deps.preferences->putUChar("light_thresh", (uint8_t)thresh);
                    changed = true;
                }
            }
        }

        if (changed) {
            request->send(200, "text/plain", "Light config saved");
        } else {
            request->send(400, "text/plain", "Invalid or missing params");
        }
    });

    // Presence Timeout (Unmanned Duration) endpoint (Task #13)
    _deps.server->on("/api/radar/timeout", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["duration"] = _deps.radar->getMaxGateDuration();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/radar/timeout", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("duration")) {
            int dur = request->getParam("duration")->value().toInt();
            if (dur >= 0 && dur <= 255) {
                bool ok = _deps.radar->setParamConfig(
                    _deps.radar->getMinGate(),
                    _deps.radar->getMaxGate(),
                    (uint8_t)dur
                );
                if (ok) {
                    _deps.preferences->putUChar("radar_dur", (uint8_t)dur);
                    request->send(200, "text/plain", "Timeout saved");
                } else {
                    request->send(500, "text/plain", "Radar command failed");
                }
            } else {
                request->send(400, "text/plain", "Invalid duration (0-255)");
            }
        } else {
            request->send(400, "text/plain", "Missing duration param");
        }
    });

#ifndef NO_BLUETOOTH
    _deps.server->on("/api/bluetooth/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.bluetooth->isRunning()) {
            _deps.bluetooth->begin(_deps.config->mqtt_id, nullptr); // Passing nullptr for configManager as we use direct Preferences in implementation for now
            _deps.bluetooth->setTimeout(600); // 10 minutes
            request->send(200, "text/plain", "Bluetooth started for 10 minutes");
        } else {
            request->send(200, "text/plain", "Bluetooth already running");
        }
    });
#endif
}

void setupAlarmRoutes() {
    _deps.server->on("/api/alarm/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["armed"] = _deps.securityMonitor->isArmed();
        doc["state"] = _deps.securityMonitor->getAlarmStateStr();
        doc["current_zone"] = _deps.securityMonitor->getCurrentZoneName();
        doc["entry_delay"] = _deps.securityMonitor->getEntryDelay() / 1000;
        doc["exit_delay"] = _deps.securityMonitor->getExitDelay() / 1000;
        doc["debounce_frames"] = _deps.securityMonitor->getAlarmDebounceFrames();
        doc["disarm_reminder"] = _deps.securityMonitor->isDisarmReminderEnabled();
        if (_deps.securityMonitor->hasLastAlarmEvent()) {
            const AlarmTriggerEvent& evt = _deps.securityMonitor->getLastAlarmEvent();
            JsonObject last = doc["last_event"].to<JsonObject>();
            last["reason"]      = evt.reason;
            last["zone"]        = evt.zone;
            last["distance_cm"] = evt.distance_cm;
            last["energy_mov"]  = evt.energy_mov;
            last["energy_stat"] = evt.energy_stat;
            last["motion_type"] = evt.motion_type;
            last["uptime_s"]    = evt.uptime_s;
            if (evt.iso_time[0] != '\0') last["time"] = evt.iso_time;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/alarm/arm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool immediate = request->hasParam("immediate") && request->getParam("immediate")->value() == "1";
        _deps.securityMonitor->setArmed(true, immediate);
        request->send(200, "text/plain", immediate ? "Armed (immediate)" : "Arming...");
    });

    _deps.server->on("/api/alarm/disarm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.securityMonitor->setArmed(false);
        request->send(200, "text/plain", "Disarmed");
    });

    _deps.server->on("/api/alarm/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (request->hasParam("entry_delay")) {
            unsigned long val = request->getParam("entry_delay")->value().toInt() * 1000;
            _deps.securityMonitor->setEntryDelay(val);
            _deps.preferences->putULong("sec_entry_dl", val);
        }
        if (request->hasParam("exit_delay")) {
            unsigned long val = request->getParam("exit_delay")->value().toInt() * 1000;
            _deps.securityMonitor->setExitDelay(val);
            _deps.preferences->putULong("sec_exit_dl", val);
        }
        if (request->hasParam("disarm_reminder")) {
            bool en = request->getParam("disarm_reminder")->value() == "1";
            _deps.securityMonitor->setDisarmReminderEnabled(en);
            _deps.preferences->putBool("sec_dis_rem", en);
        }
        if (request->hasParam("debounce_frames")) {
            uint8_t val = (uint8_t)request->getParam("debounce_frames")->value().toInt();
            _deps.securityMonitor->setAlarmDebounceFrames(val);
            _deps.preferences->putUChar("sec_debounce", val);
        }
        request->send(200, "text/plain", "Alarm config saved");
    });
}

void setupLogRoutes() {
    _deps.server->on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.systemLog->getLogJSON(doc);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/logs", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.systemLog->clear();
        request->send(200, "text/plain", "Logs cleared");
    });

    _deps.server->on("/api/events", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        uint32_t offset = request->hasParam("offset") ? request->getParam("offset")->value().toInt() : 0;
        uint32_t limit  = request->hasParam("limit")  ? request->getParam("limit")->value().toInt()  : 50;
        int8_t   type   = request->hasParam("type")   ? request->getParam("type")->value().toInt()   : -1;
        if (limit > 100) limit = 100; // cap
        JsonDocument doc;
        _deps.eventLog->getEventsJSON(doc, offset, limit, type);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/events/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        _deps.eventLog->clear();
        request->send(200, "text/plain", "History cleared");
    });

    // CSV export of events
    _deps.server->on("/api/events/csv", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        _deps.eventLog->getEventsJSON(doc, 0, 500, -1);
        JsonArray arr = doc["events"].as<JsonArray>();

        String csv = "timestamp,type,distance_cm,energy,message\r\n";
        for (JsonObject obj : arr) {
            char line[128];
            snprintf(line, sizeof(line), "%u,%u,%u,%u,\"%s\"\r\n",
                obj["ts"].as<uint32_t>(),
                obj["type"].as<uint8_t>(),
                obj["dist"].as<uint16_t>(),
                obj["en"].as<uint8_t>(),
                obj["msg"].as<const char*>());
            csv += line;
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
        response->addHeader("Content-Disposition", "attachment; filename=\"events.csv\"");
        request->send(response);
    });

    // --- Network Config (Static IP / DHCP) ---
    _deps.server->on("/api/network/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["mode"] = strlen(_deps.config->static_ip) > 0 ? "static" : "dhcp";
        doc["ip"] = strlen(_deps.config->static_ip) > 0 ? _deps.config->static_ip : ETH.localIP().toString().c_str();
        doc["gateway"] = strlen(_deps.config->static_gw) > 0 ? _deps.config->static_gw : ETH.gatewayIP().toString().c_str();
        doc["subnet"] = _deps.config->static_mask;
        doc["dns"] = strlen(_deps.config->static_dns) > 0 ? _deps.config->static_dns : ETH.dnsIP().toString().c_str();
        doc["mac"] = ETH.macAddress();
        doc["link_speed"] = ETH.linkSpeed();
        doc["full_duplex"] = ETH.fullDuplex();
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/network/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool needsReboot = false;

        if (request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            if (mode == "dhcp") {
                _deps.config->static_ip[0] = '\0';
                _deps.config->static_gw[0] = '\0';
                _deps.config->static_dns[0] = '\0';
                needsReboot = true;
            } else if (mode == "static") {
                if (request->hasParam("ip") && request->hasParam("gateway")) {
                    String ip = request->getParam("ip")->value();
                    String gw = request->getParam("gateway")->value();
                    strncpy(_deps.config->static_ip, ip.c_str(), sizeof(_deps.config->static_ip)-1);
                    strncpy(_deps.config->static_gw, gw.c_str(), sizeof(_deps.config->static_gw)-1);
                    if (request->hasParam("subnet")) {
                        strncpy(_deps.config->static_mask, request->getParam("subnet")->value().c_str(), sizeof(_deps.config->static_mask)-1);
                    }
                    if (request->hasParam("dns")) {
                        strncpy(_deps.config->static_dns, request->getParam("dns")->value().c_str(), sizeof(_deps.config->static_dns)-1);
                    }
                    needsReboot = true;
                }
            }
        }

        if (needsReboot) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Network config saved, rebooting...");
            *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "Invalid parameters");
        }
    });

    // --- Timezone Config ---
    _deps.server->on("/api/timezone", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["tz_offset"] = _deps.config->tz_offset;
        doc["dst_offset"] = _deps.config->dst_offset;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/timezone", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;
        if (request->hasParam("tz_offset")) {
            _deps.config->tz_offset = request->getParam("tz_offset")->value().toInt();
            changed = true;
        }
        if (request->hasParam("dst_offset")) {
            _deps.config->dst_offset = request->getParam("dst_offset")->value().toInt();
            changed = true;
        }
        if (changed) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Timezone saved, rebooting...");
            *_deps.shouldReboot = true;
        } else {
            request->send(400, "text/plain", "No parameters");
        }
    });

    // --- Scheduled Arm/Disarm Config ---
    _deps.server->on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["arm_time"] = _deps.config->sched_arm_time;
        doc["disarm_time"] = _deps.config->sched_disarm_time;
        doc["auto_arm_minutes"] = _deps.config->auto_arm_minutes;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    _deps.server->on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool changed = false;
        if (request->hasParam("arm_time")) {
            String t = request->getParam("arm_time")->value();
            strncpy(_deps.config->sched_arm_time, t.c_str(), sizeof(_deps.config->sched_arm_time)-1);
            changed = true;
        }
        if (request->hasParam("disarm_time")) {
            String t = request->getParam("disarm_time")->value();
            strncpy(_deps.config->sched_disarm_time, t.c_str(), sizeof(_deps.config->sched_disarm_time)-1);
            changed = true;
        }
        if (request->hasParam("auto_arm_minutes")) {
            int val = request->getParam("auto_arm_minutes")->value().toInt();
            if (val >= 0 && val <= 1440) {
                _deps.config->auto_arm_minutes = (uint16_t)val;
                changed = true;
            }
        }
        if (changed) {
            _deps.configManager->save();
            request->send(200, "text/plain", "Schedule saved");
        } else {
            request->send(400, "text/plain", "No parameters");
        }
    });
}

void setupWwwRoutes() {
    static File _uploadFile;

    // GET /api/www/info — current web asset metadata
    _deps.server->on("/api/www/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        bool hasFs = LittleFS.exists("/index.html.gz");
        doc["source"]   = hasFs ? "littlefs" : "progmem";
        doc["progmem_bytes"] = strlen(index_html);
        if (hasFs) {
            File f = LittleFS.open("/index.html.gz", "r");
            if (f) {
                doc["fs_bytes"] = f.size();
                doc["fs_mtime"] = (uint32_t)0; // LittleFS has no mtime
                f.close();
            }
        }
        doc["fs_free_kb"]  = (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
        doc["fs_total_kb"] = LittleFS.totalBytes() / 1024;
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // DELETE /api/www — remove LittleFS asset, revert to PROGMEM
    _deps.server->on("/api/www", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (LittleFS.exists("/index.html.gz")) {
            LittleFS.remove("/index.html.gz");
            request->send(200, "application/json", "{\"ok\":true,\"source\":\"progmem\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"no fs asset\"}");
        }
    });

    // POST /api/www/upload — upload index.html.gz to LittleFS
    _deps.server->on("/api/www/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!checkAuth(request)) return;
            bool ok = LittleFS.exists("/index.html.gz");
            if (ok) {
                request->send(200, "application/json", "{\"ok\":true}");
            } else {
                request->send(500, "application/json", "{\"error\":\"upload failed\"}");
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index && !checkAuth(request)) return;
            if (!index) {
                DBG("WWW", "Upload start: %s (%u bytes)", filename.c_str(), request->contentLength());
                // Ensure /www directory exists (LittleFS creates on first write)
                if (_uploadFile) _uploadFile.close();
                _uploadFile = LittleFS.open("/index.html.gz", "w");
                if (!_uploadFile) {
                    DBG("WWW", "Cannot open /index.html.gz for write");
                    return;
                }
            }
            if (_uploadFile && len > 0) {
                _uploadFile.write(data, len);
            }
            if (final) {
                if (_uploadFile) {
                    DBG("WWW", "Upload done: %u bytes", index + len);
                    _uploadFile.close();
                } else {
                    DBG("WWW", "Upload final but file not open");
                }
            }
        }
    );
}

void setupSnapshotRoutes() {
    // GET /api/config/snapshots — list available snapshots
    _deps.server->on("/api/config/snapshots", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.configSnapshot) {
            request->send(503, "application/json", "{\"error\":\"snapshots unavailable\"}");
            return;
        }
        JsonDocument doc;
        _deps.configSnapshot->getMetaJSON(doc);
        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // GET /api/config/snapshots/:slot — view content of one slot (passwords masked)
    _deps.server->on("/api/config/snapshots/0", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 0)) {
            request->send(404, "application/json", "{\"error\":\"slot 0 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });
    _deps.server->on("/api/config/snapshots/1", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 1)) {
            request->send(404, "application/json", "{\"error\":\"slot 1 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });
    _deps.server->on("/api/config/snapshots/2", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        if (!_deps.configSnapshot || !_deps.configSnapshot->getSnapshotJSON(doc, 2)) {
            request->send(404, "application/json", "{\"error\":\"slot 2 not found\"}");
            return;
        }
        String resp; serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // POST /api/config/restore?slot=N — restore NVS from snapshot, then reboot
    _deps.server->on("/api/config/restore", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (!_deps.configSnapshot || !_deps.preferences) {
            request->send(503, "application/json", "{\"error\":\"snapshots unavailable\"}");
            return;
        }
        int slot = request->hasParam("slot") ? request->getParam("slot")->value().toInt() : -1;
        bool ok = _deps.configSnapshot->restoreSnapshot(_deps.preferences, slot);
        if (!ok) {
            request->send(400, "application/json", "{\"error\":\"restore failed\"}");
            return;
        }
        request->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
        *_deps.shouldReboot = true;
    });
}

// ============================================================================
// WiFi CSI Routes — /api/csi GET / POST / actions
// Conditionally compiled: USE_CSI defines real handlers, otherwise stubs
// return 503 so the GUI can show "not compiled in" instead of 404.
// ============================================================================
void setupCSIRoutes() {
#ifdef USE_CSI
    // GET — runtime config + live values + status
    _deps.server->on("/api/csi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        JsonDocument doc;
        doc["compiled"] = true;
        doc["enabled"]  = _deps.config->csi_enabled;

        // Config (mirrors NVS-stored values)
        doc["threshold"]   = _deps.config->csi_threshold;
        doc["hysteresis"]  = _deps.config->csi_hysteresis;
        doc["window"]      = _deps.config->csi_window;
        doc["publish_ms"]  = _deps.config->csi_publish_ms;

        // Live values (only meaningful if active)
        if (_deps.csiService != nullptr && _deps.csiService->isActive()) {
            doc["active"]      = true;
            doc["motion"]      = _deps.csiService->getMotionState();
            doc["composite"]   = _deps.csiService->getCompositeScore();
            doc["turbulence"]  = _deps.csiService->getTurbulence();
            doc["phase_turb"]  = _deps.csiService->getPhaseTurbulence();
            doc["ratio_turb"]  = _deps.csiService->getRatioTurbulence();
            doc["breathing"]   = _deps.csiService->getBreathingScore();
            doc["dser"]        = _deps.csiService->getDser();
            doc["plcr"]        = _deps.csiService->getPlcr();
            doc["variance"]    = _deps.csiService->getVariance();
            doc["packets"]     = (uint32_t)_deps.csiService->getPacketCount();
            doc["pps"]         = _deps.csiService->getPacketRate();
            doc["wifi_rssi"]   = _deps.csiService->getWifiRSSI();
            doc["wifi_ssid"]   = _deps.csiService->getWifiSSID();
            doc["idle_ready"]  = _deps.csiService->isIdleInitialized();
            doc["traffic_gen"]  = _deps.csiService->isTrafficGenRunning();
            doc["traffic_port"] = _deps.csiService->getTrafficPort();
            doc["traffic_icmp"] = _deps.csiService->getTrafficICMP();
            doc["traffic_pps"]  = _deps.csiService->getTrafficRate();
            doc["wifi_ip"]      = WiFi.localIP().toString();
            doc["calibrating"] = _deps.csiService->isCalibrating();
            doc["calib_pct"]   = _deps.csiService->getCalibrationProgress();
        } else {
            doc["active"] = false;
        }

        // Fusion state
        doc["fusion_enabled"] = _deps.config->fusion_enabled;
        if (_deps.securityMonitor->isFusionActive()) {
            JsonObject fusion = doc["fusion"].to<JsonObject>();
            fusion["presence"]   = _deps.securityMonitor->isFusionPresence();
            fusion["confidence"] = _deps.securityMonitor->getFusionConfidence();
            fusion["source"]     = _deps.securityMonitor->getFusionSourceStr();
        }

        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // POST — update runtime config (any subset of params accepted)
    // Saves to NVS immediately. Some changes apply only after reboot or when
    // CSI is (re)enabled — those are flagged in the response.
    _deps.server->on("/api/csi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        bool needsRestart = false;
        bool changed = false;

        if (request->hasParam("enabled")) {
            bool en = request->getParam("enabled")->value() == "1";
            if (en != _deps.config->csi_enabled) {
                _deps.config->csi_enabled = en;
                _deps.preferences->putBool("csi_en", en);
                needsRestart = true;  // begin()/teardown WiFi requires restart
                changed = true;
            }
        }

        if (request->hasParam("threshold")) {
            float thr = request->getParam("threshold")->value().toFloat();
            if (thr >= 0.001f && thr <= 100.0f) {
                _deps.config->csi_threshold = thr;
                _deps.preferences->putFloat("csi_thr", thr);
                if (_deps.csiService) _deps.csiService->setThreshold(thr);
                changed = true;
            }
        }

        if (request->hasParam("hysteresis")) {
            float hys = request->getParam("hysteresis")->value().toFloat();
            if (hys >= 0.1f && hys <= 0.99f) {
                _deps.config->csi_hysteresis = hys;
                _deps.preferences->putFloat("csi_hyst", hys);
                if (_deps.csiService) _deps.csiService->setHysteresis(hys);
                changed = true;
            }
        }

        if (request->hasParam("window")) {
            int w = request->getParam("window")->value().toInt();
            if (w >= 10 && w <= 200) {
                _deps.config->csi_window = (uint16_t)w;
                _deps.preferences->putUShort("csi_win", (uint16_t)w);
                if (_deps.csiService) _deps.csiService->setWindowSize((uint16_t)w);
                changed = true;
            }
        }

        if (request->hasParam("publish_ms")) {
            int p = request->getParam("publish_ms")->value().toInt();
            if (p >= 100 && p <= 60000) {
                _deps.config->csi_publish_ms = (uint16_t)p;
                _deps.preferences->putUShort("csi_pubms", (uint16_t)p);
                if (_deps.csiService) _deps.csiService->setPublishInterval((uint32_t)p);
                changed = true;
            }
        }

        // Traffic generator tuning — port, ICMP mode, PPS
        if (request->hasParam("traffic_port")) {
            int p = request->getParam("traffic_port")->value().toInt();
            if (p >= 1 && p <= 65535) {
                _deps.preferences->putUShort("csi_tport", (uint16_t)p);
                if (_deps.csiService) _deps.csiService->setTrafficPort((uint16_t)p);
                changed = true;
            }
        }
        if (request->hasParam("traffic_icmp")) {
            bool icmp = request->getParam("traffic_icmp")->value() == "1";
            _deps.preferences->putBool("csi_ticmp", icmp);
            if (_deps.csiService) _deps.csiService->setTrafficICMP(icmp);
            changed = true;
        }
        if (request->hasParam("traffic_pps")) {
            int pps = request->getParam("traffic_pps")->value().toInt();
            if (pps >= 10 && pps <= 500) {
                _deps.preferences->putUInt("csi_tpps", (uint32_t)pps);
                if (_deps.csiService) _deps.csiService->setTrafficRate((uint32_t)pps);
                changed = true;
            }
        }

        // Fusion enable/disable — takes effect immediately (no restart needed)
        if (request->hasParam("fusion_enabled")) {
            bool en = request->getParam("fusion_enabled")->value() == "1";
            if (en != _deps.config->fusion_enabled) {
                _deps.config->fusion_enabled = en;
                _deps.preferences->putBool("fus_en", en);
                if (!en) {
                    _deps.securityMonitor->setCSISource(nullptr);
                } else if (_deps.csiService && _deps.csiService->isActive()) {
                    _deps.securityMonitor->setCSISource(_deps.csiService);
                }
                changed = true;
            }
        }

        JsonDocument doc;
        doc["ok"] = changed;
        doc["needs_restart"] = needsRestart;
        String resp;
        serializeJson(doc, resp);
        request->send(200, "application/json", resp);
    });

    // POST /api/csi/calibrate — sample idle variance and auto-set threshold
    _deps.server->on("/api/csi/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr || !_deps.csiService->isActive()) {
            request->send(503, "text/plain", "CSI service inactive");
            return;
        }
        uint32_t dur = 10000;
        if (request->hasParam("duration_ms")) {
            int d = request->getParam("duration_ms")->value().toInt();
            if (d >= 1000 && d <= 60000) dur = (uint32_t)d;
        }
        _deps.csiService->calibrateThreshold(dur);
        request->send(200, "text/plain", "Calibration started");
    });

    // POST /api/csi/reset_baseline — clear idle baseline (use after moving sensor)
    _deps.server->on("/api/csi/reset_baseline", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr) {
            request->send(503, "text/plain", "CSI not available");
            return;
        }
        _deps.csiService->resetIdleBaseline();
        request->send(200, "text/plain", "Idle baseline reset");
    });

    // POST /api/csi/reconnect — force WiFi.reconnect() (useful if RSSI dropped)
    _deps.server->on("/api/csi/reconnect", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        if (_deps.csiService == nullptr) {
            request->send(503, "text/plain", "CSI not available");
            return;
        }
        _deps.csiService->forceReconnect();
        request->send(200, "text/plain", "Reconnect requested");
    });

#else
    // No-CSI build — stubs so GUI can detect "not compiled in"
    _deps.server->on("/api/csi", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(200, "application/json",
                      "{\"compiled\":false,\"enabled\":false,\"active\":false}");
    });
    _deps.server->on("/api/csi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!checkAuth(request)) return;
        request->send(503, "text/plain", "CSI not compiled into this firmware");
    });
#endif
}

} // namespace WebRoutes
#endif // LITE_BUILD