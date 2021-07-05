#include <NetworkSettingsService.h>

using namespace std::placeholders; // for `_1` etc

NetworkSettingsService::NetworkSettingsService(AsyncWebServer * server, FS * fs, SecurityManager * securityManager)
    : _httpEndpoint(NetworkSettings::read, NetworkSettings::update, this, server, NETWORK_SETTINGS_SERVICE_PATH, securityManager)
    , _fsPersistence(NetworkSettings::read, NetworkSettings::update, this, fs, NETWORK_SETTINGS_FILE)
    , _lastConnectionAttempt(0) {
    // We want the device to come up in opmode=0 (WIFI_OFF), when erasing the flash this is not the default.
    // If needed, we save opmode=0 before disabling persistence so the device boots with WiFi disabled in the future.
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.mode(WIFI_OFF);
    }

    // Disable WiFi config persistance and auto reconnect
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);

    WiFi.mode(WIFI_MODE_MAX);
    WiFi.mode(WIFI_MODE_NULL);

#if defined(EMSESP_WIFI_TWEAK)
    // https: //www.esp32.com/viewtopic.php?t=12055
    esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20);
    esp_wifi_set_bandwidth(ESP_IF_WIFI_AP, WIFI_BW_HT20);
#endif

    WiFi.onEvent(std::bind(&NetworkSettingsService::WiFiEvent, this, _1));

    addUpdateHandler([&](const String & originId) { reconfigureWiFiConnection(); }, false);
}

void NetworkSettingsService::begin() {
    _fsPersistence.readFromFS();
    reconfigureWiFiConnection();
}

void NetworkSettingsService::reconfigureWiFiConnection() {
    // reset last connection attempt to force loop to reconnect immediately
    _lastConnectionAttempt = 0;

    // disconnect and de-configure wifi
    if (WiFi.disconnect(true)) {
        _stopping = true;
    }
}

void NetworkSettingsService::loop() {
    unsigned long currentMillis = millis();
    if (!_lastConnectionAttempt || (uint32_t)(currentMillis - _lastConnectionAttempt) >= WIFI_RECONNECTION_DELAY) {
        _lastConnectionAttempt = currentMillis;
        manageSTA();
    }
}

void NetworkSettingsService::manageSTA() {
    // Abort if already connected, or if we have no SSID
    if (WiFi.isConnected() || _state.ssid.length() == 0) {
        return;
    }

    // Connect or reconnect as required
    if ((WiFi.getMode() & WIFI_STA) == 0) {
        if (_state.staticIPConfig) {
            WiFi.config(_state.localIP, _state.gatewayIP, _state.subnetMask, _state.dnsIP1, _state.dnsIP2); // configure for static IP
        } else {
            WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // configure for DHCP
        }

        WiFi.setHostname(_state.hostname.c_str());                // set hostname
        WiFi.begin(_state.ssid.c_str(), _state.password.c_str()); // attempt to connect to the network
    }
}

// handles both WiFI and Ethernet
void NetworkSettingsService::WiFiEvent(WiFiEvent_t event) {
    switch (event) {
    case SYSTEM_EVENT_STA_DISCONNECTED:
        WiFi.disconnect(true);
        break;

    case SYSTEM_EVENT_STA_STOP:
        if (_stopping) {
            _lastConnectionAttempt = 0;
            _stopping              = false;
        }
        break;

    case SYSTEM_EVENT_ETH_START:
        if (_state.staticIPConfig) {
            ETH.config(_state.localIP, _state.gatewayIP, _state.subnetMask, _state.dnsIP1, _state.dnsIP2); // configure for static IP
        }
        break;

    default:
        break;
    }
}
