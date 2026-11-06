// Жалоба «не открывается сайт» в панель Susanin (web_enable=1).
// Тот же API, что у кнопки панели: POST /api/complain (форма ip=<домен|IP>),
// авторизация заголовком X-Auth-Token (панель с web_token) — запросы из
// service worker вне CORS, поэтому расширение ставится на любой LAN-адрес.
const DEFAULT_PANEL = "http://192.168.0.1:8087";

async function settings() {
  const s = await chrome.storage.local.get(["panelUrl", "token"]);
  return { base: (s.panelUrl || DEFAULT_PANEL).replace(/\/+$/, ""), token: s.token || "" };
}

async function complain(domain) {
  const { base, token } = await settings();
  const headers = { "Content-Type": "application/x-www-form-urlencoded" };
  if (token) headers["X-Auth-Token"] = token;
  try {
    const r = await fetch(base + "/api/complain", {
      method: "POST",
      headers: headers,
      body: "ip=" + encodeURIComponent(domain)
    });
    const j = await r.json().catch(function () { return {}; });
    notify(domain, (r.ok && j.ok) ? "отправлен в тестовую пробу" : (j.err || "панель отказала"));
    return j;
  } catch (e) {
    notify(domain, "панель недоступна: " + base);
    return { ok: false };
  }
}

function notify(domain, text) {
  chrome.notifications.create({
    type: "basic",
    iconUrl: chrome.runtime.getURL("icon16.png"),
    title: "Susanin: " + domain,
    message: text
  });
}

chrome.runtime.onInstalled.addListener(function () {
  chrome.contextMenus.create({
    id: "susanin-vpn",
    title: "Susanin Helper: в VPN",
    contexts: ["page", "link", "selection"]
  });
});

chrome.contextMenus.onClicked.addListener(function (info) {
  const url = info.linkUrl || info.srcUrl || info.pageUrl;
  if (!url) return;
  let host;
  try { host = new URL(url).hostname; } catch (e) { return; }
  if (host) complain(host);
});

chrome.runtime.onMessage.addListener(function (msg, _sender, sendResponse) {
  if (msg && msg.complain) {
    complain(msg.complain).then(sendResponse);
    return true;
  }
});
