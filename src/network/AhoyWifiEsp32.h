//-----------------------------------------------------------------------------
// 2024 Ahoy, https://ahoydtu.de
// Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
//-----------------------------------------------------------------------------

#ifndef __AHOY_WIFI_ESP32_H__
#define __AHOY_WIFI_ESP32_H__

#if defined(ESP32)
#include <functional>
#include <AsyncUDP.h>
#include <WiFi.h>
#include "AhoyNetwork.h"
#include "ESPAsyncWebServer.h"

class AhoyWifi : public AhoyNetwork {
    public:
        virtual void begin() override {
            mAp.enable();

            if(strlen(mConfig->sys.stationSsid) == 0)
                return; // no station wifi defined


            WiFi.disconnect(); // clean up
            WiFi.setHostname(mConfig->sys.deviceName);
            #if !defined(AP_ONLY)
                WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
                WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
                setStaticIp();
                WiFi.begin(mConfig->sys.stationSsid, mConfig->sys.stationPwd, WIFI_ALL_CHANNEL_SCAN);
                mWifiConnecting = true;
                mLastConnectAttemptMs = millis();
                if(0 == mOutageStartMs)
                    mOutageStartMs = mLastConnectAttemptMs;

                DBGPRINT(F("connect to network '"));
                DBGPRINT(mConfig->sys.stationSsid);
                DBGPRINTLN(F("'"));
            #endif
        }

        void tickNetworkLoop() override {
            AhoyNetwork::tickNetworkLoop();
            if(mAp.isEnabled())
                mAp.tickLoop();

            if(mStatus != NetworkState::GOT_IP) {
                const uint32_t now = millis();

                if(0 == mOutageStartMs)
                    mOutageStartMs = now;

                if(mRetryScheduled && ((now - mLastConnectAttemptMs) >= WIFI_RETRY_INTERVAL_MS)) {
                    mRetryScheduled = false;
                    triggerReconnect();
                }

                if(!mLongRecoveryDone && ((now - mOutageStartMs) >= WIFI_LONG_RECOVERY_MS)) {
                    mLongRecoveryDone = true;
                    DPRINTLN(DBG_WARN, F("Network long outage, force WiFi reconnect"));
                    WiFi.disconnect(true, false);
                    triggerReconnect();
                }
            }
        }

        virtual String getIp(void) override {
            return WiFi.localIP().toString();
        }

        virtual String getMac(void) override {
            return WiFi.macAddress();
        }

    private:
        virtual void OnEvent(WiFiEvent_t event) override {
            switch(event) {
                case SYSTEM_EVENT_STA_CONNECTED:
                    if(NetworkState::CONNECTED != mStatus) {
                        mStatus = NetworkState::CONNECTED;
                        mWifiConnecting = false;
                        DPRINTLN(DBG_INFO, F("Network connected"));
                    }
                    break;

                case SYSTEM_EVENT_STA_GOT_IP:
                    mStatus = NetworkState::GOT_IP;
                    mRetryCount = 0;
                    mRetryScheduled = false;
                    mLongRecoveryDone = false;
                    mOutageStartMs = 0;
                    if(mAp.isEnabled())
                        mAp.disable();

                    if(!mConnected) {
                        mConnected = true;
                        ah::welcome(WiFi.localIP().toString(), F("Station"));
                        MDNS.begin(mConfig->sys.deviceName);
                        mOnNetworkCB(true);
                    }
                    break;

                case ARDUINO_EVENT_WIFI_STA_LOST_IP:
                    [[fallthrough]];
                case ARDUINO_EVENT_WIFI_STA_STOP:
                    [[fallthrough]];
                case SYSTEM_EVENT_STA_DISCONNECTED:
                    mStatus = NetworkState::DISCONNECTED;
                    if(mConnected) {
                        mConnected = false;
                        mOnNetworkCB(false);
                        MDNS.end();
                    }
                    scheduleReconnect();
                    break;

                default:
                    break;
            }
        }

        virtual void setStaticIp() override {
            setupIp([this](IPAddress ip, IPAddress gateway, IPAddress mask, IPAddress dns1, IPAddress dns2) -> bool {
                return WiFi.config(ip, gateway, mask, dns1, dns2);
            });
        }

        void scheduleReconnect() {
            if(mRetryScheduled)
                return;

            mRetryScheduled = true;
            if(0 == mOutageStartMs)
                mOutageStartMs = millis();
        }

        void triggerReconnect() {
            if(strlen(mConfig->sys.stationSsid) == 0)
                return;

            ++mRetryCount;

            DBGPRINT(F("Network reconnect try #"));
            DBGPRINT(mRetryCount);
            DBGPRINTLN(F(""));

            WiFi.disconnect(false, false);
            setStaticIp();
            WiFi.begin(mConfig->sys.stationSsid, mConfig->sys.stationPwd, WIFI_ALL_CHANNEL_SCAN);
            mWifiConnecting = true;
            mLastConnectAttemptMs = millis();

            if(mRetryCount >= WIFI_AP_FALLBACK_RETRIES)
                mAp.enable();
        }

    private:
        static constexpr uint8_t WIFI_AP_FALLBACK_RETRIES = 4;
        static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
        static constexpr uint32_t WIFI_LONG_RECOVERY_MS = 30UL * 60UL * 1000UL;

        uint32_t mLastConnectAttemptMs = 0;
        uint32_t mOutageStartMs = 0;
        uint8_t mRetryCount = 0;
        bool mRetryScheduled = false;
        bool mLongRecoveryDone = false;
};

#endif /*ESP32*/
#endif /*__AHOY_WIFI_ESP32_H__*/
