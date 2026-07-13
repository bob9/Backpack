#if defined(TARGET_TIMER_BACKPACK) && defined(PLATFORM_ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <MD5Builder.h>
#include <time.h>
#include <sys/time.h>

#include "devwifi_proxies.h"
#include "msp.h"
#include "msptypes.h"
#include "logging.h"

// Goggle test page, ported from the ELRS Netpack's HTTP test server.
// Served from the timer backpack's WiFi mode so a race director can fire
// OSD/channel/time/DVR test messages at any goggle bind phrase without
// RotorHazard in the loop.
//
// ESP-NOW is not running in WiFi mode (Timer_main only initialises it for
// normal operation), so this module brings it up on demand. The goggles only
// listen on channel 1, and only accept packets whose sender MAC is their own
// UID, so every send needs a MAC spoof - always done on whichever interface
// is NOT carrying the browser session:
//   - AP mode: the SoftAP is on channel 1; send from the idle STA interface.
//   - Home network: the radio is locked to the router's channel (must be 1);
//     send from a hidden SoftAP brought up alongside the station link.

static MSP testMsp;
static bool espnowStarted = false;
static bool peerRegistered = false;
static uint8_t currentPeer[6];
static wifi_interface_t peerInterface = WIFI_IF_STA;

// Derive a binding UID the same way the ExpressLRS configurator does: the
// first 6 bytes of MD5("-DMY_BINDING_PHRASE=\"<phrase>\""), first byte made
// even (ESP-NOW requires a unicast MAC).
static void UidFromPhrase(const String &phrase, uint8_t uid[6])
{
    MD5Builder md5;
    md5.begin();
    md5.add("-DMY_BINDING_PHRASE=\"" + phrase + "\"");
    md5.calculate();
    uint8_t digest[16];
    md5.getBytes(digest);
    memcpy(uid, digest, 6);
    uid[0] &= ~0x01;
}

static const char *EspnowEnsureReady(wifi_interface_t *sendInterface)
{
    WiFiMode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF)
    {
        return "wifi not started";
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        // On the home network the browser session rides the STA link, so it
        // must not be touched. The radio is parked on the router's channel;
        // the goggles only listen on channel 1.
        if (WiFi.channel() != 1)
        {
            static char msg[130];
            snprintf(msg, sizeof(msg),
                     "home network is on channel %d but goggles listen on channel 1 - set the router's 2.4GHz channel to 1, or use Access Point mode",
                     WiFi.channel());
            return msg;
        }
        if (mode != WIFI_AP_STA)
        {
            // Hidden SoftAP purely to open the AP interface for ESP-NOW
            WiFi.softAP("ExpressLRS Timer Backpack", "expresslrs", 1, 1);
        }
        *sendInterface = WIFI_IF_AP;
    }
    else if (mode == WIFI_AP || mode == WIFI_AP_STA)
    {
        // Browser is on the SoftAP (channel 1); send from the idle STA interface
        if (mode == WIFI_AP)
        {
            WiFi.mode(WIFI_AP_STA);
        }
        *sendInterface = WIFI_IF_STA;
    }
    else
    {
        return "not connected to a network and not an access point - start the Access Point from the main page";
    }

    if (!espnowStarted)
    {
        if (esp_now_init() != ESP_OK)
        {
            return "ESP-NOW init failed";
        }
        espnowStarted = true;
    }
    return NULL;
}

static void PacketInit(mspPacket_t *packet, uint16_t function)
{
    packet->reset();
    packet->makeCommand();
    packet->function = function;
}

// Send count packets to the goggle bound to uid
static const char *SendToUid(const uint8_t uid[6], mspPacket_t *packets, size_t count)
{
    wifi_interface_t sendInterface;
    const char *err = EspnowEnsureReady(&sendInterface);
    if (err != NULL)
    {
        return err;
    }

    if (peerRegistered && (memcmp(currentPeer, uid, 6) != 0 || peerInterface != sendInterface))
    {
        esp_now_del_peer(currentPeer);
        peerRegistered = false;
    }
    if (!peerRegistered)
    {
        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, uid, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        peerInfo.ifidx = sendInterface;
        if (esp_now_add_peer(&peerInfo) != ESP_OK)
        {
            return "ESP-NOW failed to add peer";
        }
        memcpy(currentPeer, uid, 6);
        peerInterface = sendInterface;
        peerRegistered = true;
    }

    // The VRx only accepts packets whose sender MAC is its own UID
    esp_wifi_set_mac(sendInterface, currentPeer);

    for (size_t i = 0; i < count; i++)
    {
        uint8_t packetSize = testMsp.getTotalPacketSize(&packets[i]);
        uint8_t nowDataOutput[packetSize];
        if (!testMsp.convertToByteArray(&packets[i], nowDataOutput))
        {
            return "packet conversion failed";
        }
        if (esp_now_send(currentPeer, nowDataOutput, packetSize) != ESP_OK)
        {
            return "ESP-NOW send failed";
        }
        delay(3); // let the radio drain between packets
    }
    return NULL;
}

// ── test actions ────────────────────────────────────────────────────────────

static const char *DoOsd(const uint8_t uid[6], const String &text, int row)
{
    size_t len = text.length();
    if (len == 0 || len > 50)
        return "message must be 1-50 characters";
    if (row < 0 || row > 17)
        return "row must be 0-17";

    mspPacket_t seq[3];
    PacketInit(&seq[0], MSP_ELRS_SET_OSD);
    seq[0].addByte(0x02); // clear

    PacketInit(&seq[1], MSP_ELRS_SET_OSD);
    seq[1].addByte(0x03); // stage text
    seq[1].addByte((uint8_t)row);
    seq[1].addByte((uint8_t)((50 - len) / 2)); // centered
    seq[1].addByte(0);
    for (size_t i = 0; i < len; i++)
    {
        // The goggles index their Betaflight-layout OSD font directly with
        // this byte: only 0x20-0x5F match ASCII (no lowercase - that range
        // holds the arrow glyphs), so fold to uppercase and blank the rest.
        char c = (char)toupper((unsigned char)text[i]);
        if (c < 0x20 || c > 0x5F)
            c = ' ';
        seq[1].addByte(c);
    }

    PacketInit(&seq[2], MSP_ELRS_SET_OSD);
    seq[2].addByte(0x04); // display

    return SendToUid(uid, seq, 3);
}

static const char *DoClear(const uint8_t uid[6])
{
    mspPacket_t seq[2];
    PacketInit(&seq[0], MSP_ELRS_SET_OSD);
    seq[0].addByte(0x02);
    PacketInit(&seq[1], MSP_ELRS_SET_OSD);
    seq[1].addByte(0x04);
    return SendToUid(uid, seq, 2);
}

static const char *DoChannel(const uint8_t uid[6], int index)
{
    if (index < 0 || index > 47)
        return "invalid channel index";
    mspPacket_t p;
    PacketInit(&p, MSP_SET_VTX_CONFIG);
    p.addByte((uint8_t)index);
    return SendToUid(uid, &p, 1);
}

static const char *DoTime(const uint8_t uid[6])
{
    time_t now = time(NULL);
    if (now < 1704067200) // 1 Jan 2024
        return "timer clock not set - use the Set clock button first";
    struct tm timeData;
    localtime_r(&now, &timeData);

    mspPacket_t p;
    PacketInit(&p, MSP_ELRS_BACKPACK_SET_RTC);
    p.addByte(timeData.tm_year);
    p.addByte(timeData.tm_mon);
    p.addByte(timeData.tm_mday);
    p.addByte(timeData.tm_hour);
    p.addByte(timeData.tm_min);
    p.addByte(timeData.tm_sec);
    return SendToUid(uid, &p, 1);
}

static const char *DoDvr(const uint8_t uid[6], const String &label)
{
    size_t len = label.length();
    if (len == 0 || len > 32)
        return "label must be 1-32 characters";
    mspPacket_t p;
    PacketInit(&p, MSP_ELRS_BACKPACK_SET_DVR_NAME);
    for (size_t i = 0; i < len; i++)
        p.addByte(label[i]);
    return SendToUid(uid, &p, 1);
}

// ── HTTP handlers ───────────────────────────────────────────────────────────

static const char TEST_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ELRS Timer Backpack - Goggle Test</title>
<style>
 body{font-family:system-ui,sans-serif;background:#14161a;color:#e8e8e8;max-width:560px;margin:24px auto;padding:0 16px}
 h1{font-size:20px} h2{font-size:14px;margin:22px 0 8px;color:#9ad}
 .card{background:#1d2026;border:1px solid #2c3038;border-radius:10px;padding:14px;margin-bottom:12px}
 label{display:block;font-size:12px;color:#aab;margin-bottom:4px}
 input,select{width:100%;box-sizing:border-box;background:#12141a;color:#e8e8e8;border:1px solid #363b45;border-radius:6px;padding:8px;font-size:14px;margin-bottom:10px}
 button{background:#2b6cb0;color:#fff;border:0;border-radius:6px;padding:9px 14px;font-size:14px;cursor:pointer;margin:2px 4px 2px 0}
 button:hover{background:#3182ce} button.warn{background:#805ad5}
 #status{margin-top:12px;padding:10px;border-radius:6px;font-size:13px;display:none}
 .ok{background:#1c4532;color:#9ae6b4} .err{background:#553030;color:#feb2b2}
 small{color:#889}
</style></head><body>
<h1>Goggle test</h1>
<div class="card">
 <label>Pilot's ELRS bind phrase</label>
 <input id="phrase" placeholder="the phrase flashed/bound to the goggles">
 <small>The UID is derived exactly like the ELRS Configurator, so type the phrase as the pilot set it.
 Works from the backpack's own access point, or over a home network whose 2.4GHz band is on channel 1
 (the goggles only listen there).</small>
</div>
<div class="card"><h2>OSD</h2>
 <label>Message</label><input id="text" value="TIMER TEST OK" maxlength="50">
 <label>Row (0-17)</label><input id="row" type="number" min="0" max="17" value="4">
 <button onclick="send('osd')">Show message</button>
 <button class="warn" onclick="send('clear')">Clear OSD</button>
 <small>The goggle OSD font is uppercase-only; lowercase is sent as capitals.</small>
</div>
<div class="card"><h2>Channel change</h2>
 <select id="channel">
  <optgroup label="Raceband">
   <option value="32">R1</option><option value="33">R2</option><option value="34" selected>R3</option>
   <option value="35">R4</option><option value="36">R5</option><option value="37">R6</option>
   <option value="38">R7</option><option value="39">R8</option>
  </optgroup>
  <optgroup label="Other">
   <option value="24">F1</option><option value="25">F2</option><option value="27">F4</option><option value="16">E1</option>
  </optgroup>
  <optgroup label="Low Band">
   <option value="40">L1</option><option value="41">L2</option><option value="42">L3</option><option value="43">L4</option>
   <option value="44">L5</option><option value="45">L6</option><option value="46">L7</option><option value="47">L8</option>
  </optgroup>
 </select>
 <button onclick="send('channel')">Change channel</button>
 <small>Low Band needs goggle firmware with remote band switching.</small>
</div>
<div class="card"><h2>Timer clock</h2>
 <div style="margin-bottom:8px;font-size:14px">Timer time: <b id="tclock">loading...</b></div>
 <button onclick="setClock()">Set from this device's clock</button>
 <small>Needed before sending a time sync; also settable via NTP from the main page when on a home network.</small>
</div>
<div class="card"><h2>Extras</h2>
 <button onclick="send('time')">Send time sync</button>
 <label style="margin-top:10px">DVR label</label><input id="dvr" value="TimerTest" maxlength="32">
 <button onclick="send('dvr')">Send DVR name</button>
 <small>Time sets the goggle clock; DVR names the goggles' next recording.</small>
</div>
<div id="status"></div>
<script>
const $=id=>document.getElementById(id);
$('phrase').value=localStorage.getItem('bindphrase')||'';
async function refreshClock(){
 try{const r=await fetch('/test/clock');const j=await r.json();
  document.getElementById('tclock').textContent=j.set?j.time:'not set';
 }catch(e){document.getElementById('tclock').textContent='?'}
}
refreshClock();setInterval(refreshClock,10000);
async function setClock(){
 const n=new Date();
 const st=$('status');st.style.display='block';st.className='';st.textContent='Setting clock...';
 const body=new URLSearchParams({action:'setclock',y:n.getFullYear(),mo:n.getMonth()+1,d:n.getDate(),
   h:n.getHours(),mi:n.getMinutes(),s:n.getSeconds()});
 try{
  const r=await fetch('/test/api',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();
  st.className=j.ok?'ok':'err';
  st.textContent=j.ok?'Timer clock set':'Error: '+j.error;
  refreshClock();
 }catch(e){st.className='err';st.textContent='Request failed: '+e}
}
async function send(action){
 const phrase=$('phrase').value.trim();
 const st=$('status'); st.style.display='block';
 if(!phrase){st.className='err';st.textContent='Enter the bind phrase first';return}
 localStorage.setItem('bindphrase',phrase);
 const body=new URLSearchParams({action,phrase,text:$('text').value,row:$('row').value,
   channel:$('channel').value,label:$('dvr').value});
 st.className='';st.textContent='Sending...';
 try{
  const r=await fetch('/test/api',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();
  st.className=j.ok?'ok':'err';
  st.textContent=j.ok?('Sent '+action+' to '+j.uid+' - check the goggles'):('Error: '+j.error);
 }catch(e){st.className='err';st.textContent='Request failed: '+e}
}
</script></body></html>
)HTML";

static void WebTimerTestPage(AsyncWebServerRequest *request)
{
    request->send_P(200, "text/html", TEST_PAGE);
}

// Current timer clock as JSON, for the test page's clock card
static void WebTimerTestClock(AsyncWebServerRequest *request)
{
    char resp[96];
    time_t now = time(NULL);
    if (now < 1704067200)
    {
        snprintf(resp, sizeof(resp), "{\"set\":false}");
    }
    else
    {
        struct tm timeData;
        char buf[32];
        localtime_r(&now, &timeData);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeData);
        snprintf(resp, sizeof(resp), "{\"set\":true,\"time\":\"%s\"}", buf);
    }
    request->send(200, "application/json", resp);
}

static void WebTimerTestApi(AsyncWebServerRequest *request)
{
    String action = request->arg("action");

    // Setting the timer clock needs no bind phrase - the browser sends its
    // own local wall time
    if (action == "setclock")
    {
        int y = request->arg("y").toInt();
        if (y < 2024)
        {
            request->send(200, "application/json", "{\"ok\":false,\"error\":\"bad clock values\"}");
            return;
        }
        struct tm timeData = {};
        timeData.tm_year = y - 1900;
        timeData.tm_mon = request->arg("mo").toInt() - 1;
        timeData.tm_mday = request->arg("d").toInt();
        timeData.tm_hour = request->arg("h").toInt();
        timeData.tm_min = request->arg("mi").toInt();
        timeData.tm_sec = request->arg("s").toInt();
        timeval tv = {mktime(&timeData), 0};
        settimeofday(&tv, NULL);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
    }

    String phrase = request->arg("phrase");
    if (phrase.length() == 0)
    {
        request->send(200, "application/json", "{\"ok\":false,\"error\":\"bind phrase required\"}");
        return;
    }

    uint8_t uid[6];
    UidFromPhrase(phrase, uid);

    const char *err = "unknown action";
    if (action == "osd")
    {
        err = DoOsd(uid, request->arg("text"), request->arg("row").toInt());
    }
    else if (action == "clear")
    {
        err = DoClear(uid);
    }
    else if (action == "channel")
    {
        err = DoChannel(uid, request->arg("channel").toInt());
    }
    else if (action == "time")
    {
        err = DoTime(uid);
    }
    else if (action == "dvr")
    {
        err = DoDvr(uid, request->arg("label"));
    }

    char resp[160];
    if (err == NULL)
    {
        DBGLN("goggle test %s sent to uid %02x%02x%02x%02x%02x%02x", action.c_str(),
              uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"uid\":\"%02X:%02X:%02X:%02X:%02X:%02X\"}",
                 uid[0], uid[1], uid[2], uid[3], uid[4], uid[5]);
    }
    else
    {
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", err);
    }
    request->send(200, "application/json", resp);
}

void WebTimerTestInit(AsyncWebServer &server)
{
    server.on("/test", HTTP_GET, WebTimerTestPage);
    server.on("/test/clock", HTTP_GET, WebTimerTestClock);
    server.on("/test/api", HTTP_POST, WebTimerTestApi);
}

#endif /* defined(TARGET_TIMER_BACKPACK) && defined(PLATFORM_ESP32) */
