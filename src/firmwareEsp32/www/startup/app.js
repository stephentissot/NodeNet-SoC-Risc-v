const statusBox = document.getElementById('statusBox');
const settingsBox = document.getElementById('settingsBox');
const wifiForm = document.getElementById('wifiForm');
const settingsForm = document.getElementById('settingsForm');
const ssidInput = document.getElementById('ssid');
const passwordInput = document.getElementById('password');
const languageQuick = document.getElementById('languageQuick');
const languageSelect = document.getElementById('languageSelect');
const anonymousAccessInput = document.getElementById('anonymousAccess');

const languages = [
  { value: 'uk', label: 'English (UK)', htmlLang: 'en-GB' },
  { value: 'fr', label: 'Francais', htmlLang: 'fr' },
  { value: 'de', label: 'Deutsch', htmlLang: 'de' },
  { value: 'es', label: 'Espanol', htmlLang: 'es' },
  { value: 'zh-Hans', label: '简体中文', htmlLang: 'zh-Hans' },
];

const translations = {
  uk: {
    title: 'NodeNet Setup',
    pageTitle: 'Wi-Fi Setup',
    lead: 'Connect the ESP32 to your local network while keeping a maintenance access point available.',
    language: 'Language',
    ssid: 'SSID',
    password: 'Password',
    passwordPlaceholder: 'Leave empty for an open network',
    saveWifi: 'Save and connect',
    settingsTitle: 'Access and language',
    settingsLead: 'These settings are stored in ESP32 flash.',
    anonymousLabel: 'Anonymous access',
    anonymousHint: 'When disabled, the startup site remains available for setup but the PLC app requires the future user system.',
    saveSettings: 'Save settings',
    hint: 'The maintenance access point stays available for local support.',
    openApp: 'Open PLC application',
    wifiStatusLoading: 'Loading Wi-Fi status...',
    settingsLoading: 'Loading settings...',
    ap: 'AP',
    stationConfigured: 'Station configured',
    yes: 'yes',
    no: 'no',
    stationConnected: 'Station connected',
    ipPending: 'IP pending',
    currentSsid: 'Current SSID',
    savingWifi: 'Saving Wi-Fi settings...',
    wifiSaved: 'Configuration saved. Station reconnect in progress...',
    wifiStatusFailed: 'Unable to read Wi-Fi status:',
    settingsSaved: 'Settings saved to flash.',
    settingsFailed: 'Unable to save settings:',
    httpError: 'HTTP error',
  },
  fr: {
    title: 'Configuration NodeNet',
    pageTitle: 'Configuration Wi-Fi',
    lead: 'Connecte l\'ESP32 a ton reseau local tout en gardant un point d\'acces de maintenance.',
    language: 'Langue',
    ssid: 'SSID',
    password: 'Mot de passe',
    passwordPlaceholder: 'Laisser vide pour reseau ouvert',
    saveWifi: 'Enregistrer et connecter',
    settingsTitle: 'Acces et langue',
    settingsLead: 'Ces parametres sont stockes dans la flash ESP32.',
    anonymousLabel: 'Acces anonyme',
    anonymousHint: 'Quand il est desactive, le site startup reste disponible pour la configuration mais l\'application PLC attendra le futur systeme utilisateurs.',
    saveSettings: 'Enregistrer les parametres',
    hint: 'Le point d\'acces de maintenance reste disponible pour le support local.',
    openApp: 'Ouvrir l\'application PLC',
    wifiStatusLoading: 'Chargement du statut Wi-Fi...',
    settingsLoading: 'Chargement des parametres...',
    ap: 'PA',
    stationConfigured: 'Station configuree',
    yes: 'oui',
    no: 'non',
    stationConnected: 'Station connectee',
    ipPending: 'IP en attente',
    currentSsid: 'SSID courant',
    savingWifi: 'Enregistrement Wi-Fi en cours...',
    wifiSaved: 'Configuration enregistree. Reconnexion station en cours...',
    wifiStatusFailed: 'Impossible de lire le statut Wi-Fi :',
    settingsSaved: 'Parametres enregistres en flash.',
    settingsFailed: 'Impossible d\'enregistrer les parametres :',
    httpError: 'Erreur HTTP',
  },
  de: {
    title: 'NodeNet Einrichtung',
    pageTitle: 'WLAN-Einrichtung',
    lead: 'Verbinde den ESP32 mit deinem lokalen Netzwerk und behalte dabei einen Wartungszugangspunkt.',
    language: 'Sprache',
    ssid: 'SSID',
    password: 'Passwort',
    passwordPlaceholder: 'Fur ein offenes Netzwerk leer lassen',
    saveWifi: 'Speichern und verbinden',
    settingsTitle: 'Zugriff und Sprache',
    settingsLead: 'Diese Einstellungen werden im ESP32-Flash gespeichert.',
    anonymousLabel: 'Anonymer Zugriff',
    anonymousHint: 'Wenn deaktiviert, bleibt die Startup-Seite fur die Einrichtung erreichbar, die PLC-App wartet jedoch auf das spatere Benutzersystem.',
    saveSettings: 'Einstellungen speichern',
    hint: 'Der Wartungszugangspunkt bleibt fur lokalen Support verfugbar.',
    openApp: 'PLC-Anwendung offnen',
    wifiStatusLoading: 'WLAN-Status wird geladen...',
    settingsLoading: 'Einstellungen werden geladen...',
    ap: 'AP',
    stationConfigured: 'Station konfiguriert',
    yes: 'ja',
    no: 'nein',
    stationConnected: 'Station verbunden',
    ipPending: 'IP ausstehend',
    currentSsid: 'Aktuelle SSID',
    savingWifi: 'WLAN-Einstellungen werden gespeichert...',
    wifiSaved: 'Konfiguration gespeichert. Station verbindet sich neu...',
    wifiStatusFailed: 'WLAN-Status konnte nicht gelesen werden:',
    settingsSaved: 'Einstellungen im Flash gespeichert.',
    settingsFailed: 'Einstellungen konnten nicht gespeichert werden:',
    httpError: 'HTTP-Fehler',
  },
  es: {
    title: 'Configuracion NodeNet',
    pageTitle: 'Configuracion Wi-Fi',
    lead: 'Conecta el ESP32 a tu red local manteniendo un punto de acceso de mantenimiento.',
    language: 'Idioma',
    ssid: 'SSID',
    password: 'Contrasena',
    passwordPlaceholder: 'Dejar vacio para red abierta',
    saveWifi: 'Guardar y conectar',
    settingsTitle: 'Acceso e idioma',
    settingsLead: 'Estos ajustes se almacenan en la flash del ESP32.',
    anonymousLabel: 'Acceso anonimo',
    anonymousHint: 'Si se desactiva, el sitio de arranque sigue disponible para configuracion, pero la aplicacion PLC esperara al futuro sistema de usuarios.',
    saveSettings: 'Guardar ajustes',
    hint: 'El punto de acceso de mantenimiento sigue disponible para soporte local.',
    openApp: 'Abrir aplicacion PLC',
    wifiStatusLoading: 'Cargando estado Wi-Fi...',
    settingsLoading: 'Cargando ajustes...',
    ap: 'AP',
    stationConfigured: 'Estacion configurada',
    yes: 'si',
    no: 'no',
    stationConnected: 'Estacion conectada',
    ipPending: 'IP pendiente',
    currentSsid: 'SSID actual',
    savingWifi: 'Guardando configuracion Wi-Fi...',
    wifiSaved: 'Configuracion guardada. Reconexion de la estacion en curso...',
    wifiStatusFailed: 'No se pudo leer el estado Wi-Fi:',
    settingsSaved: 'Ajustes guardados en flash.',
    settingsFailed: 'No se pudieron guardar los ajustes:',
    httpError: 'Error HTTP',
  },
  'zh-Hans': {
    title: 'NodeNet 设置',
    pageTitle: 'Wi-Fi 设置',
    lead: '将 ESP32 连接到本地网络，同时保留维护接入点。',
    language: '语言',
    ssid: 'SSID',
    password: '密码',
    passwordPlaceholder: '开放网络请留空',
    saveWifi: '保存并连接',
    settingsTitle: '访问与语言',
    settingsLead: '这些设置会保存到 ESP32 Flash。',
    anonymousLabel: '匿名访问',
    anonymousHint: '关闭后，启动站点仍可用于配置，但 PLC 应用需要后续的用户系统。',
    saveSettings: '保存设置',
    hint: '维护接入点仍可用于本地支持。',
    openApp: '打开 PLC 应用',
    wifiStatusLoading: '正在加载 Wi-Fi 状态...',
    settingsLoading: '正在加载设置...',
    ap: '接入点',
    stationConfigured: '站点已配置',
    yes: '是',
    no: '否',
    stationConnected: '站点已连接',
    ipPending: 'IP 等待中',
    currentSsid: '当前 SSID',
    savingWifi: '正在保存 Wi-Fi 设置...',
    wifiSaved: '配置已保存，正在重新连接站点...',
    wifiStatusFailed: '无法读取 Wi-Fi 状态：',
    settingsSaved: '设置已保存到 Flash。',
    settingsFailed: '无法保存设置：',
    httpError: 'HTTP 错误',
  },
};

let currentLanguage = 'uk';

function t(key) {
  return translations[currentLanguage]?.[key] || translations.uk[key] || key;
}

function getLanguageMeta(language) {
  return languages.find((entry) => entry.value === language) || languages[0];
}

function applyTranslations() {
  const meta = getLanguageMeta(currentLanguage);
  document.documentElement.lang = meta.htmlLang;
  document.title = t('title');
  document.getElementById('pageTitle').textContent = t('pageTitle');
  document.getElementById('leadText').textContent = t('lead');
  document.getElementById('languageLabelTop').textContent = t('language');
  document.getElementById('ssidLabel').textContent = t('ssid');
  document.getElementById('passwordLabel').textContent = t('password');
  passwordInput.placeholder = t('passwordPlaceholder');
  document.getElementById('wifiSubmit').textContent = t('saveWifi');
  document.getElementById('settingsTitle').textContent = t('settingsTitle');
  document.getElementById('settingsLead').textContent = t('settingsLead');
  document.getElementById('languageLabel').textContent = t('language');
  document.getElementById('anonymousLabel').textContent = t('anonymousLabel');
  document.getElementById('anonymousHint').textContent = t('anonymousHint');
  document.getElementById('settingsSubmit').textContent = t('saveSettings');
  document.getElementById('hintText').textContent = t('hint');
  document.getElementById('appLink').textContent = t('openApp');
}

function fillLanguageSelect(select) {
  select.innerHTML = '';
  for (const entry of languages) {
    const option = document.createElement('option');
    option.value = entry.value;
    option.textContent = entry.label;
    select.appendChild(option);
  }
}

function setLanguage(language) {
  currentLanguage = translations[language] ? language : 'uk';
  languageQuick.value = currentLanguage;
  languageSelect.value = currentLanguage;
  applyTranslations();
}

function renderStatus(status) {
  const lines = [
    `${t('ap')}: ${status.ap_ssid} (${status.ap_ip || 'n/a'})`,
    `${t('stationConfigured')}: ${status.sta_has_credentials ? t('yes') : t('no')}`,
    `${t('stationConnected')}: ${status.sta_connected ? `${t('yes')} (${status.sta_ip || t('ipPending')})` : t('no')}`,
  ];

  if (status.sta_ssid) {
    lines.push(`${t('currentSsid')}: ${status.sta_ssid}`);
  }

  statusBox.textContent = lines.join('\n');
}

function renderSettings(settings) {
  setLanguage(settings.language || 'uk');
  anonymousAccessInput.checked = settings.anonymous;
  settingsBox.textContent = `${t('language')}: ${getLanguageMeta(currentLanguage).label}\n${t('anonymousLabel')}: ${settings.anonymous ? t('yes') : t('no')}`;
}

async function loadStatus() {
  const response = await fetch('/api/wifi/status');
  if (!response.ok) {
    throw new Error(`${t('httpError')} ${response.status}`);
  }
  const status = await response.json();
  renderStatus(status);
  if (status.sta_ssid) {
    ssidInput.value = status.sta_ssid;
  }
}

async function loadSettings() {
  const response = await fetch('/api/settings');
  if (!response.ok) {
    throw new Error(`${t('httpError')} ${response.status}`);
  }
  const payload = await response.json();
  renderSettings(payload.settings || payload);
}

wifiForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  statusBox.textContent = t('savingWifi');

  const response = await fetch('/api/wifi/config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      ssid: ssidInput.value.trim(),
      password: passwordInput.value,
    }),
  });

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    statusBox.textContent = payload.error || `${t('httpError')} ${response.status}`;
    return;
  }

  passwordInput.value = '';
  statusBox.textContent = t('wifiSaved');
  await loadStatus();
});

settingsForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  settingsBox.textContent = t('settingsLoading');

  const response = await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      language: languageSelect.value,
      anonymous: anonymousAccessInput.checked,
    }),
  });

  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    settingsBox.textContent = `${t('settingsFailed')} ${payload.error || `${t('httpError')} ${response.status}`}`;
    return;
  }

  renderSettings(payload.settings || payload);
  settingsBox.textContent = t('settingsSaved');
});

languageQuick.addEventListener('change', () => {
  languageSelect.value = languageQuick.value;
  setLanguage(languageQuick.value);
});

languageSelect.addEventListener('change', () => {
  languageQuick.value = languageSelect.value;
  setLanguage(languageSelect.value);
});

fillLanguageSelect(languageQuick);
fillLanguageSelect(languageSelect);
setLanguage('uk');
statusBox.textContent = t('wifiStatusLoading');
settingsBox.textContent = t('settingsLoading');

Promise.all([loadSettings(), loadStatus()]).catch((error) => {
  statusBox.textContent = `${t('wifiStatusFailed')} ${error.message}`;
  settingsBox.textContent = `${t('settingsFailed')} ${error.message}`;
});
