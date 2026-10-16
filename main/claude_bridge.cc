#include "claude_bridge.h"
#include "claude_protocol.h"
#include "radio_player.h"
#include "radio_ui.h"
#include "radio_display_settings.h"
#include "bsp_display.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "nvs.h"
#include "cJSON.h"
#include "lwip/inet.h"
#include <sys/time.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace {
constexpr char kTag[] = "claude_bridge";
constexpr size_t kResponseLimit = 2048;
struct Config { char host[64]; char token[65]; uint16_t port; };
struct Key { uint32_t generation; uint32_t when; bool up; };
Config s_config{}, s_current{};
std::mutex s_config_mutex;
std::atomic<uint32_t> s_active_generation{0};
std::atomic<bool> s_configured{false};
std::atomic<bool> s_network_connected{false};
QueueHandle_t s_keys;
TaskHandle_t s_task;
claude_protocol::State s_state{};
char s_challenge[33]{};
uint32_t s_last_idle;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL); }

bool digest(const std::string &message, char output[65]) {
    unsigned char hash[32]{};
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_hmac(info, reinterpret_cast<const unsigned char *>(s_current.token),
            std::strlen(s_current.token), reinterpret_cast<const unsigned char *>(message.data()),
            message.size(), hash) != 0) return false;
    for (size_t i = 0; i < sizeof(hash); ++i) std::snprintf(output + i*2, 3, "%02x", hash[i]);
    return true;
}

struct HttpResult { std::string body; std::string signature; };
esp_err_t http_event(esp_http_client_event_t *event) {
    auto *result = static_cast<HttpResult *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        strcasecmp(event->header_key, "X-Passport-Response") == 0) {
        result->signature = event->header_value;
    }
    return ESP_OK;
}

bool http_request(const char *method, const char *path, const std::string &body,
                  std::string *response, int *status) {
    unsigned char random[16]; esp_fill_random(random, sizeof(random));
    char nonce[33]{};
    for (size_t i=0; i<sizeof(random); ++i) std::snprintf(nonce+i*2, 3, "%02x", random[i]);
    char signature[65]{};
    if (!digest(std::string(method)+"\n"+path+"\n"+nonce+"\n"+body, signature)) return false;
    std::string url = std::string("http://")+s_current.host+":"+std::to_string(s_current.port)+path;
    HttpResult result;
    esp_http_client_config_t cfg{};
    cfg.url = url.c_str();
    cfg.method = std::strcmp(method,"POST")==0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    cfg.timeout_ms = 1200;
    cfg.buffer_size = 512;
    cfg.buffer_size_tx = 512;
    cfg.disable_auto_redirect = true;
    cfg.event_handler = http_event;
    cfg.user_data = &result;
    auto client = esp_http_client_init(&cfg);
    if (!client) return false;
    bool ok = esp_http_client_set_header(client,"X-Passport-Nonce",nonce)==ESP_OK &&
              esp_http_client_set_header(client,"X-Claude-Bridge-Signature",signature)==ESP_OK &&
              esp_http_client_set_header(client,"Content-Type","application/json")==ESP_OK &&
              esp_http_client_open(client, body.size())==ESP_OK;
    if (ok && !body.empty()) ok = esp_http_client_write(client,body.data(),body.size())==static_cast<int>(body.size());
    if (ok) ok = esp_http_client_fetch_headers(client)>=0;
    int code = ok ? esp_http_client_get_status_code(client) : 0;
    std::array<char,512> buffer{};
    int count = 0;
    while (ok && (count = esp_http_client_read(client,buffer.data(),buffer.size()))>0) {
        if (result.body.size()+static_cast<size_t>(count)>kResponseLimit) { ok=false; break; }
        result.body.append(buffer.data(),count);
    }
    if (count<0 || (ok && !esp_http_client_is_complete_data_received(client))) ok=false;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    // Authenticate the response as well: a LAN attacker cannot substitute a prompt or an acknowledgement.
    char expected[65]{};
    ok = ok && digest(std::string(nonce)+"\n"+std::to_string(code)+"\n"+result.body,expected);
    unsigned mismatch = result.signature.size()==64 ? 0 : 1;
    if (result.signature.size()==64) for (size_t i=0;i<64;++i) mismatch |= expected[i]^result.signature[i];
    if (status) *status=code;
    if (response) *response=std::move(result.body);
    return ok && mismatch==0;
}

void finish() {
    s_active_generation.store(0,std::memory_order_release);
    radio_ui_hide_permission();
    radio_display_hold_awake(false);
    radio_player_set_interrupted(false);
    claude_protocol::clear(&s_state);
}

void send_decision(claude_protocol::Decision decision) {
    // Disable input before any blocking I/O. No second key can authorize another request.
    s_active_generation.store(0,std::memory_order_release);
    cJSON *root=cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root,"id",s_state.request.id);
        cJSON_AddStringToObject(root,"challenge",s_challenge);
        cJSON_AddStringToObject(root,"decision",claude_protocol::decision_text(decision));
        char *raw=cJSON_PrintUnformatted(root);
        if (raw) {
            int status=0; std::string response;
            bool ok=http_request("POST","/v1/decision",raw,&response,&status);
            if (!ok || status!=200) ESP_LOGW(kTag,"Decision acknowledgement missing; check Claude session");
            cJSON_free(raw);
        }
        cJSON_Delete(root);
    }
    finish();
}

void poll() {
    std::string response; int status=0;
    const bool ok=http_request("GET","/v1/pending","",&response,&status);
    if (!ok || (status!=200 && status!=204)) {
        if (s_state.active) { s_state.decision=claude_protocol::Decision::Deny; finish(); }
        radio_ui_set_bridge_status("Claude 离线");
        return;
    }
    radio_ui_set_bridge_status("Claude 已连接");
    if (status==204) { if (s_state.active) finish(); return; }
    cJSON *root=cJSON_ParseWithLength(response.data(),response.size());
    if (!root) { if (s_state.active) finish(); return; }
    const char *id=cJSON_GetStringValue(cJSON_GetObjectItem(root,"id"));
    const char *tool=cJSON_GetStringValue(cJSON_GetObjectItem(root,"tool"));
    const char *summary=cJSON_GetStringValue(cJSON_GetObjectItem(root,"summary"));
    const char *cwd=cJSON_GetStringValue(cJSON_GetObjectItem(root,"cwd"));
    const char *challenge=cJSON_GetStringValue(cJSON_GetObjectItem(root,"challenge"));
    auto *timeout=cJSON_GetObjectItem(root,"deadline_ms");
    const bool valid=id && std::strlen(id)<sizeof(s_state.request.id) && tool && summary &&
        challenge && std::strlen(challenge)==32 && cJSON_IsNumber(timeout) && timeout->valuedouble>0;
    if (s_state.active && (!valid || std::strcmp(s_state.request.id,id)!=0)) finish();
    if (valid && !s_state.active) {
        uint32_t remaining=static_cast<uint32_t>(std::min(120000.0,timeout->valuedouble));
        if (claude_protocol::start(&s_state,id,tool,summary,cwd,now_ms(),remaining)) {
            std::snprintf(s_challenge,sizeof(s_challenge),"%s",challenge);
            radio_player_set_interrupted(true);
            if (!radio_player_wait_idle(1500)) {
                ESP_LOGW(kTag,"Audio did not stop in time; denying request");
                send_decision(claude_protocol::Decision::Deny);
                cJSON_Delete(root);
                return;
            }
            radio_display_hold_awake(true);
            if (radio_ui_show_permission(tool,summary,cwd)) {
                s_active_generation.store(s_state.generation,std::memory_order_release);
            } else send_decision(claude_protocol::Decision::Deny);
        }
    }
    cJSON_Delete(root);
}

void bridge_task(void *) {
    for (;;) {
        Config next;
        { std::lock_guard<std::mutex> lock(s_config_mutex); next=s_config; }
        if (std::memcmp(&next,&s_current,sizeof(next))!=0) {
            if (s_state.active) send_decision(claude_protocol::Decision::Deny);
            s_current=next; s_last_idle=0;
        }
        if (!s_current.host[0]) {
            radio_ui_set_bridge_status("Claude 未配对 / USB");
            vTaskDelay(pdMS_TO_TICKS(700)); continue;
        }
        // Do not enter lwIP before Wi-Fi has initialized the TCP/IP stack.
        if (!s_network_connected.load(std::memory_order_acquire)) {
            if (s_state.active) finish();
            radio_ui_set_bridge_status("Claude 等待 Wi-Fi");
            vTaskDelay(pdMS_TO_TICKS(350)); continue;
        }
        Key message{};
        if (s_state.active && static_cast<int32_t>(now_ms()-s_state.request.deadline_ms)>=0)
            send_decision(claude_protocol::Decision::Deny);
        while (xQueueReceive(s_keys,&message,0)==pdTRUE) {
            if (!s_state.active || message.generation!=s_state.generation) continue;
            auto event=claude_protocol::key(&s_state,message.up,!message.up,now_ms());
            if (event!=claude_protocol::Event::None) send_decision(s_state.decision);
        }
        poll(); // Active requests keep their host lease alive, and detect disconnect/cancellation.
        if (!s_state.active && (s_last_idle==0 || now_ms()-s_last_idle>=60000)) {
            std::string response; int status=0;
            if (http_request("GET","/v1/idle","",&response,&status) && status==200) {
                cJSON *root=cJSON_ParseWithLength(response.data(),response.size());
                if (root) {
                    const char *date=cJSON_GetStringValue(cJSON_GetObjectItem(root,"date"));
                    const char *lunar=cJSON_GetStringValue(cJSON_GetObjectItem(root,"lunar"));
                    const char *weather=cJSON_GetStringValue(cJSON_GetObjectItem(root,"weather"));
                    radio_ui_set_idle(date,lunar,weather);
                    auto *epoch=cJSON_GetObjectItem(root,"epoch");
                    if (cJSON_IsNumber(epoch) && epoch->valuedouble>1700000000) {
                        timeval time{}; time.tv_sec=static_cast<time_t>(epoch->valuedouble); settimeofday(&time,nullptr);
                    }
                    cJSON_Delete(root);
                }
            }
            s_last_idle=now_ms();
        }
        vTaskDelay(pdMS_TO_TICKS(350));
    }
}
} // namespace

extern "C" esp_err_t claude_bridge_init(void) {
    nvs_handle_t handle;
    if (nvs_open("claude",NVS_READONLY,&handle)==ESP_OK) {
        size_t size=sizeof(s_config);
        if (nvs_get_blob(handle,"pair_v2",&s_config,&size)!=ESP_OK || size!=sizeof(s_config)) s_config={};
        nvs_close(handle);
    }
    // NVS is untrusted persisted input, including after interrupted upgrades.
    in_addr saved_address{};
    if (!std::memchr(s_config.host, '\0', sizeof(s_config.host)) ||
        !std::memchr(s_config.token, '\0', sizeof(s_config.token)) ||
        inet_pton(AF_INET,s_config.host,&saved_address)!=1 || !s_config.port ||
        std::strlen(s_config.token)!=64 || std::strspn(s_config.token,"0123456789abcdef")!=64) s_config={};
    s_configured.store(s_config.host[0] && s_config.token[0]);
    s_keys=xQueueCreate(4,sizeof(Key));
    if (!s_keys || xTaskCreate(bridge_task,"claude_bridge",6144,nullptr,4,&s_task)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
extern "C" void claude_bridge_set_network(bool connected) { s_network_connected.store(connected,std::memory_order_release); }
extern "C" uint32_t claude_bridge_generation(void) { return s_active_generation.load(std::memory_order_acquire); }
extern "C" bool claude_bridge_handle_key(bsp_btn_t button,bsp_btn_ev_t event,uint32_t generation) {
    const uint32_t current=claude_bridge_generation();
    if (!current && !generation) return false;
    if (current && generation==current && event==BSP_BTN_CLICK && (button==BSP_BTN_UP || button==BSP_BTN_DOWN)) {
        Key key{generation,now_ms(),button==BSP_BTN_UP};
        (void)xQueueSend(s_keys,&key,0); // Overflow cannot grant permission.
    }
    return true; // Consume OK/long/release/stale input while a prompt is visible.
}
extern "C" bool claude_bridge_is_configured(void) { return s_configured.load(); }
extern "C" esp_err_t claude_bridge_configure(const char *host,const char *token,uint16_t port) {
    in_addr address{};
    if (!host || !token || inet_pton(AF_INET,host,&address)!=1 || !port || std::strlen(token)!=64 ||
        std::strspn(token,"0123456789abcdef")!=64) return ESP_ERR_INVALID_ARG;
    Config next{};
    std::snprintf(next.host,sizeof(next.host),"%s",host);
    std::snprintf(next.token,sizeof(next.token),"%s",token); next.port=port;
    std::lock_guard<std::mutex> lock(s_config_mutex);
    nvs_handle_t handle;
    esp_err_t error=nvs_open("claude",NVS_READWRITE,&handle);
    if (error!=ESP_OK) return error;
    error=nvs_set_blob(handle,"pair_v2",&next,sizeof(next));
    if (error==ESP_OK) error=nvs_commit(handle);
    nvs_close(handle);
    if (error==ESP_OK) { s_config=next; s_configured.store(true); }
    return error;
}

extern "C" bool claude_bridge_radio_url(char *url, size_t capacity) {
    if (!url || !capacity) return false;
    std::lock_guard<std::mutex> lock(s_config_mutex);
    if (!s_config.host[0] || !s_config.port) { url[0] = '\0'; return false; }
    const int length = std::snprintf(url, capacity, "http://%s:%u/radio/phoenix.mp3",
                                     s_config.host, s_config.port);
    return length > 0 && static_cast<size_t>(length) < capacity;
}
