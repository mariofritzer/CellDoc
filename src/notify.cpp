#include "notify.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "evlog.h"
#include "settings.h"

namespace notify {

struct Msg {
  char title[64];
  char text[200];
};
static QueueHandle_t q = nullptr;

bool configured() { return settings.ntfy.length() || (settings.tgTok.length() && settings.tgChat.length()); }

static String urlEncode(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += char(c);
    else {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

static int post(const String& url, const String& body, const char* ctype, const String& title) {
  WiFiClientSecure tls;
  WiFiClient plain;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  bool ok = url.startsWith("https") ? http.begin(tls, url) : http.begin(plain, url);
  if (!ok) return -1;
  http.addHeader("Content-Type", ctype);
  if (title.length()) http.addHeader("Title", title);
  int code = http.POST(body);
  http.end();
  return code;
}

static void task(void*) {
  Msg m;
  for (;;) {
    if (xQueueReceive(q, &m, portMAX_DELAY) != pdTRUE) continue;
    if (WiFi.status() != WL_CONNECTED) continue;
    String title = m.title, text = m.text;
    if (settings.ntfy.length()) {
      String u = settings.ntfy;
      if (!u.startsWith("http")) u = "https://ntfy.sh/" + u;
      // Titel im Kopf: nur ASCII sicher, Umlaute stehen im Text
      String t;
      for (char c : title) t += (uint8_t(c) < 128) ? c : '?';
      int code = post(u, text, "text/plain; charset=utf-8", t);
      if (code < 200 || code >= 300) evlog::warn(0, "Benachrichtigung über ntfy fehlgeschlagen (" + String(code) + ")");
    }
    if (settings.tgTok.length() && settings.tgChat.length()) {
      String u = "https://api.telegram.org/bot" + settings.tgTok + "/sendMessage";
      String b = "chat_id=" + urlEncode(settings.tgChat) + "&text=" + urlEncode(title + "\n" + text);
      int code = post(u, b, "application/x-www-form-urlencoded", "");
      if (code < 200 || code >= 300) evlog::warn(0, "Benachrichtigung über Telegram fehlgeschlagen (" + String(code) + ")");
    }
  }
}

void send(char kind, const String& title, const String& text) {
  bool want = kind == 't' || (kind == 'd' && settings.nDone) || (kind == 'e' && settings.nErr) || (kind == 'c' && settings.nChg);
  if (!want || !configured()) return;
  if (!q) {
    q = xQueueCreate(4, sizeof(Msg));
    if (!q) return;
    if (xTaskCreate(task, "notify", 8192, nullptr, 1, nullptr) != pdPASS) return;
  }
  Msg m;
  String t = settings.hostname + ": " + title;
  strlcpy(m.title, t.c_str(), sizeof(m.title));
  strlcpy(m.text, text.c_str(), sizeof(m.text));
  xQueueSend(q, &m, 0);
}

}  // namespace notify
