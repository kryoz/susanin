const $ = function (id) { return document.getElementById(id); };
const DEFAULT_PANEL = "http://192.168.0.1:8087";
let curHost = "";

function send(domain) {
  if (!domain) return;
  chrome.runtime.sendMessage({ complain: domain }, function (j) {
    $("d").value = "";
    $("cur").textContent = (j && j.ok)
      ? domain + " — отправлен в тестовую пробу"
      : domain + " — не принято (смотри уведомление)";
  });
}

async function settings() {
  const s = await chrome.storage.local.get(["panelUrl", "token"]);
  return { base: (s.panelUrl || DEFAULT_PANEL).replace(/\/+$/, ""), token: s.token || "" };
}
async function listOp(list, op) {
  if (!curHost) {
    $("lst").className = "err";
    $("lst").textContent = "нет текущего сайта";
    return;
  }
  const { base, token } = await settings();
  const headers = { "Content-Type": "application/x-www-form-urlencoded" };
  if (token) headers["X-Auth-Token"] = token;
  try {
    const r = await fetch(base + "/api/list", {
      method: "POST",
      headers: headers,
      body: "list=" + list + "&op=" + op + "&value=" + encodeURIComponent(curHost)
    });
    const j = await r.json().catch(function () { return {}; });
    const ok = r.ok && j.ok;
    $("lst").className = ok ? "" : "err";
    $("lst").textContent = ok
      ? curHost + (op === "add" ? " — в " + list : " — убран из " + list)
      : (j.error || "панель отказала");
  } catch (e) {
    $("lst").className = "err";
    $("lst").textContent = "нет связи";
  }
}


chrome.tabs.query({ active: true, currentWindow: true }, function (tabs) {
  try {
    curHost = new URL(tabs[0].url).hostname;
    $("cur").textContent = "Текущий сайт: " + curHost;
  } catch (e) { $("cur").textContent = "Служебная страница"; }
});

$("go").onclick = function () { send($("d").value.trim() || curHost); };
$("curBtn").onclick = function () { send(curHost); };
$("alAdd").onclick = function () { listOp("vpn_always", "add"); };
$("alDel").onclick = function () { listOp("vpn_always", "del"); };
$("nvAdd").onclick = function () { listOp("vpn_never", "add"); };
$("nvDel").onclick = function () { listOp("vpn_never", "del"); };
$("d").addEventListener("keydown", function (e) {
  if (e.key === "Enter") send(this.value.trim());
});

$("save").onclick = async function () {
  await chrome.storage.local.set({
    panelUrl: $("url").value.trim(),
    token: $("tok").value.trim()
  });
  $("st").textContent = "сохранено";
};

$("test").onclick = async function () {
  const { base, token } = await settings();
  const headers = token ? { "X-Auth-Token": token } : {};
  try {
    const r = await fetch(base + "/api/status", { headers: headers });
    const j = await r.json();
    $("st").className = "";
    $("st").textContent = "панель " + j.version;
  } catch (e) {
    $("st").className = "err";
    $("st").textContent = "нет связи";
  }
};

(async function init() {
  const s = await chrome.storage.local.get(["panelUrl", "token"]);
  $("url").value = s.panelUrl || DEFAULT_PANEL;
  $("tok").value = s.token || "";
})();
