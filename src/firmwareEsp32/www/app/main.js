import { createApp, computed, onMounted, ref } from '../vendor/vue.esm-browser.prod.js';

const languages = [
  { value: 'uk', label: 'English (UK)', htmlLang: 'en-GB' },
  { value: 'fr', label: 'Francais', htmlLang: 'fr' },
  { value: 'de', label: 'Deutsch', htmlLang: 'de' },
  { value: 'es', label: 'Espanol', htmlLang: 'es' },
  { value: 'zh-Hans', label: '简体中文', htmlLang: 'zh-Hans' },
];

const translations = {
  uk: {
    title: 'NodeNet PLC',
    eyebrow: 'NodeNet PLC Console',
    heroTitle: 'Real-time configuration and states',
    heroLead: 'Vue 3 runs locally on the ESP32. Snapshot changes are announced over WebSocket, then the UI reloads the full states over HTTP.',
    language: 'Language',
    accessTitle: 'Access and language',
    accessLead: 'These settings are persisted in ESP32 flash.',
    anonymousLabel: 'Anonymous access',
    anonymousHint: 'When disabled, public access to the PLC app, PLC APIs and WebSocket is blocked until the future user system is added.',
    saveSettings: 'Save settings',
    boot: 'Boot',
    snapshotLoaded: 'snapshot loaded',
    snapshotLoading: 'loading snapshot',
    wifi: 'Wi-Fi',
    setupVia: 'setup via',
    connectionInProgress: 'connection in progress',
    socket: 'Socket',
    sequence: 'Sequence',
    plcSnapshot: 'PLC snapshot',
    pointsLoaded: 'points loaded',
    refreshSnapshot: 'Request snapshot refresh',
    point: 'Point',
    type: 'Type',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Timestamp',
    loading: 'loading',
    settingsSaved: 'Settings saved.',
    settingsSaveFailed: 'Unable to save settings:',
  },
  fr: {
    title: 'NodeNet PLC',
    eyebrow: 'Console PLC NodeNet',
    heroTitle: 'Configuration et etats temps reel',
    heroLead: 'Vue 3 tourne localement sur l\'ESP32. Les changements de snapshot sont annonces via WebSocket, puis l\'UI recharge les etats complets via HTTP.',
    language: 'Langue',
    accessTitle: 'Acces et langue',
    accessLead: 'Ces parametres sont persistants dans la flash ESP32.',
    anonymousLabel: 'Acces anonyme',
    anonymousHint: 'Quand il est desactive, l\'acces public a l\'application PLC, aux API PLC et au WebSocket est bloque en attendant le futur systeme utilisateurs.',
    saveSettings: 'Enregistrer les parametres',
    boot: 'Boot',
    snapshotLoaded: 'snapshot charge',
    snapshotLoading: 'chargement en cours',
    wifi: 'Wi-Fi',
    setupVia: 'setup via',
    connectionInProgress: 'connexion en cours',
    socket: 'Socket',
    sequence: 'Sequence',
    plcSnapshot: 'Snapshot PLC',
    pointsLoaded: 'points charges',
    refreshSnapshot: 'Relancer un snapshot',
    point: 'Point',
    type: 'Type',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Timestamp',
    loading: 'chargement',
    settingsSaved: 'Parametres enregistres.',
    settingsSaveFailed: 'Impossible d\'enregistrer les parametres :',
  },
  de: {
    title: 'NodeNet PLC',
    eyebrow: 'NodeNet PLC Konsole',
    heroTitle: 'Echtzeit-Konfiguration und Zustande',
    heroLead: 'Vue 3 lauft lokal auf dem ESP32. Snapshot-Anderungen werden per WebSocket gemeldet, danach ladt die UI die vollstandigen Zustande per HTTP neu.',
    language: 'Sprache',
    accessTitle: 'Zugriff und Sprache',
    accessLead: 'Diese Einstellungen werden dauerhaft im ESP32-Flash gespeichert.',
    anonymousLabel: 'Anonymer Zugriff',
    anonymousHint: 'Wenn deaktiviert, ist der offentliche Zugriff auf PLC-App, PLC-APIs und WebSocket gesperrt, bis das spatere Benutzersystem verfugbar ist.',
    saveSettings: 'Einstellungen speichern',
    boot: 'Boot',
    snapshotLoaded: 'Snapshot geladen',
    snapshotLoading: 'Snapshot wird geladen',
    wifi: 'Wi-Fi',
    setupVia: 'Setup uber',
    connectionInProgress: 'Verbindung lauft',
    socket: 'Socket',
    sequence: 'Sequenz',
    plcSnapshot: 'PLC-Snapshot',
    pointsLoaded: 'Punkte geladen',
    refreshSnapshot: 'Snapshot neu anfordern',
    point: 'Punkt',
    type: 'Typ',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Zeitstempel',
    loading: 'laden',
    settingsSaved: 'Einstellungen gespeichert.',
    settingsSaveFailed: 'Einstellungen konnten nicht gespeichert werden:',
  },
  es: {
    title: 'NodeNet PLC',
    eyebrow: 'Consola PLC NodeNet',
    heroTitle: 'Configuracion y estados en tiempo real',
    heroLead: 'Vue 3 se ejecuta localmente en el ESP32. Los cambios de snapshot se anuncian por WebSocket y la UI recarga los estados completos por HTTP.',
    language: 'Idioma',
    accessTitle: 'Acceso e idioma',
    accessLead: 'Estos ajustes se guardan de forma persistente en la flash del ESP32.',
    anonymousLabel: 'Acceso anonimo',
    anonymousHint: 'Si se desactiva, el acceso publico a la app PLC, a las API PLC y al WebSocket queda bloqueado hasta que exista el futuro sistema de usuarios.',
    saveSettings: 'Guardar ajustes',
    boot: 'Arranque',
    snapshotLoaded: 'snapshot cargado',
    snapshotLoading: 'cargando snapshot',
    wifi: 'Wi-Fi',
    setupVia: 'configuracion por',
    connectionInProgress: 'conexion en curso',
    socket: 'Socket',
    sequence: 'Secuencia',
    plcSnapshot: 'Snapshot PLC',
    pointsLoaded: 'puntos cargados',
    refreshSnapshot: 'Solicitar refresco del snapshot',
    point: 'Punto',
    type: 'Tipo',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Marca de tiempo',
    loading: 'cargando',
    settingsSaved: 'Ajustes guardados.',
    settingsSaveFailed: 'No se pudieron guardar los ajustes:',
  },
  'zh-Hans': {
    title: 'NodeNet PLC',
    eyebrow: 'NodeNet PLC 控制台',
    heroTitle: '实时配置与状态',
    heroLead: 'Vue 3 在 ESP32 本地运行。快照变化通过 WebSocket 通知，然后界面通过 HTTP 重新加载完整状态。',
    language: '语言',
    accessTitle: '访问与语言',
    accessLead: '这些设置会持久保存到 ESP32 Flash。',
    anonymousLabel: '匿名访问',
    anonymousHint: '关闭后，在未来用户系统完成之前，PLC 应用、PLC API 和 WebSocket 的公共访问都会被阻止。',
    saveSettings: '保存设置',
    boot: '启动',
    snapshotLoaded: '快照已加载',
    snapshotLoading: '正在加载快照',
    wifi: 'Wi-Fi',
    setupVia: '通过以下方式配置',
    connectionInProgress: '连接中',
    socket: 'Socket',
    sequence: '序列',
    plcSnapshot: 'PLC 快照',
    pointsLoaded: '个点已加载',
    refreshSnapshot: '请求刷新快照',
    point: '点位',
    type: '类型',
    flags: '标志',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: '时间戳',
    loading: '加载中',
    settingsSaved: '设置已保存。',
    settingsSaveFailed: '无法保存设置：',
  },
};

const socketState = ref('offline');
const systemInfo = ref(null);
const states = ref([]);
const errorText = ref('');
const settingsMessage = ref('');
const lastSequence = ref(null);
const currentLanguage = ref('uk');
const anonymousAccess = ref(true);
let socket = null;

function t(key) {
  return translations[currentLanguage.value]?.[key] || translations.uk[key] || key;
}

function getLanguageMeta(language) {
  return languages.find((entry) => entry.value === language) || languages[0];
}

function applyDocumentLanguage() {
  const meta = getLanguageMeta(currentLanguage.value);
  document.documentElement.lang = meta.htmlLang;
  document.title = t('title');
}

function syncSettings(settings) {
  if (!settings) {
    return;
  }

  currentLanguage.value = translations[settings.language] ? settings.language : 'uk';
  anonymousAccess.value = settings.anonymous;
  applyDocumentLanguage();
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  const payload = await response.json();
  if (!response.ok) {
    throw new Error(payload.error || `HTTP ${response.status}`);
  }
  return payload;
}

async function loadSystemInfo() {
  systemInfo.value = await fetchJson('/api/system/info');
  syncSettings(systemInfo.value.settings);
}

async function loadStates() {
  const snapshot = await fetchJson('/api/plc/states/snapshot');
  states.value = snapshot.records;
  lastSequence.value = snapshot.sequence;
}

async function requestRefresh() {
  await fetchJson('/api/plc/states/refresh', { method: 'POST' });
}

async function saveSettings() {
  const payload = await fetchJson('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      language: currentLanguage.value,
      anonymous: anonymousAccess.value,
    }),
  });

  systemInfo.value = {
    ...(systemInfo.value || {}),
    settings: payload.settings,
  };
  syncSettings(payload.settings);
  settingsMessage.value = t('settingsSaved');
}

function connectSocket() {
  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${protocol}://${window.location.host}/ws`);
  socketState.value = 'connecting';

  socket.addEventListener('open', () => {
    socketState.value = 'online';
  });

  socket.addEventListener('close', () => {
    socketState.value = 'offline';
    window.setTimeout(connectSocket, 1500);
  });

  socket.addEventListener('error', () => {
    socketState.value = 'error';
  });

  socket.addEventListener('message', async (event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'boot_progress') {
      if (systemInfo.value) {
        systemInfo.value.boot = message.boot;
      }
      return;
    }

    if (message.type === 'wifi_status') {
      if (systemInfo.value) {
        systemInfo.value.wifi = message.wifi;
      }
      return;
    }

    if (message.type === 'settings') {
      if (systemInfo.value) {
        systemInfo.value.settings = message.settings;
      }
      syncSettings(message.settings);
      return;
    }

    if (message.type === 'plc_snapshot_available') {
      if (message.snapshot.sequence !== lastSequence.value || message.snapshot.loaded_points !== states.value.length) {
        await loadStates();
        await loadSystemInfo();
      }
    }
  });
}

const app = {
  setup() {
    const pointCount = computed(() => systemInfo.value?.snapshot?.point_count || 0);
    const loadedPoints = computed(() => systemInfo.value?.snapshot?.loaded_points || 0);
    const wifiSummary = computed(() => {
      if (!systemInfo.value) {
        return t('loading');
      }
      const wifi = systemInfo.value.wifi;
      if (wifi.sta_connected) {
        return `${wifi.sta_ssid} (${wifi.sta_ip})`;
      }
      if (wifi.sta_has_credentials) {
        return `${wifi.sta_ssid} (${t('connectionInProgress')})`;
      }
      return `${t('setupVia')} ${wifi.ap_ssid}`;
    });

    onMounted(async () => {
      try {
        await Promise.all([loadSystemInfo(), loadStates()]);
        connectSocket();
      } catch (error) {
        errorText.value = error.message;
      }
    });

    return {
      errorText,
      anonymousAccess,
      currentLanguage,
      loadedPoints,
      languages,
      pointCount,
      requestRefresh,
      saveSettings: async () => {
        try {
          await saveSettings();
        } catch (error) {
          settingsMessage.value = `${t('settingsSaveFailed')} ${error.message}`;
        }
      },
      settingsMessage,
      socketState,
      states,
      systemInfo,
      t,
      wifiSummary,
    };
  },
  template: `
    <main class="layout">
      <section class="hero">
        <div class="hero-top">
          <div>
            <p class="eyebrow">{{ t('eyebrow') }}</p>
            <h1>{{ t('heroTitle') }}</h1>
            <p class="lead">{{ t('heroLead') }}</p>
          </div>
          <div class="toolbar panel metric" v-if="systemInfo">
            <span>{{ t('language') }}</span>
            <select v-model="currentLanguage">
              <option v-for="language in languages" :key="language.value" :value="language.value">{{ language.label }}</option>
            </select>
            <button @click="saveSettings">{{ t('saveSettings') }}</button>
          </div>
        </div>
      </section>

      <section class="panel settings-panel" v-if="systemInfo">
        <div class="settings-grid">
          <div>
            <h2>{{ t('accessTitle') }}</h2>
            <p class="lead">{{ t('accessLead') }}</p>
          </div>
          <label class="field">
            <span>{{ t('language') }}</span>
            <select v-model="currentLanguage">
              <option v-for="language in languages" :key="language.value" :value="language.value">{{ language.label }}</option>
            </select>
          </label>
          <label class="toggle-card">
            <input v-model="anonymousAccess" type="checkbox">
            <span>
              <strong>{{ t('anonymousLabel') }}</strong>
              <small>{{ t('anonymousHint') }}</small>
            </span>
          </label>
          <button @click="saveSettings">{{ t('saveSettings') }}</button>
        </div>
      </section>

      <section class="grid" v-if="systemInfo">
        <article class="panel metric">
          <h2>{{ t('boot') }}</h2>
          <strong>{{ systemInfo.boot.percent }}%</strong>
          <span>{{ systemInfo.boot.snapshot_complete ? t('snapshotLoaded') : t('snapshotLoading') }}</span>
        </article>
        <article class="panel metric">
          <h2>{{ t('wifi') }}</h2>
          <strong>{{ wifiSummary }}</strong>
          <span>AP {{ systemInfo.wifi.ap_ssid }} / {{ systemInfo.wifi.ap_ip }}</span>
        </article>
        <article class="panel metric">
          <h2>{{ t('socket') }}</h2>
          <strong>{{ socketState }}</strong>
          <span>{{ t('sequence') }} {{ systemInfo.snapshot.sequence }}</span>
        </article>
      </section>

      <section class="panel actions" v-if="systemInfo">
        <div>
          <h2>{{ t('plcSnapshot') }}</h2>
          <p>{{ loadedPoints }} / {{ pointCount }} {{ t('pointsLoaded') }}</p>
        </div>
        <button @click="requestRefresh">{{ t('refreshSnapshot') }}</button>
      </section>

      <section class="panel" v-if="settingsMessage">
        <div class="metric">{{ settingsMessage }}</div>
      </section>

      <section class="panel error" v-if="errorText">
        {{ errorText }}
      </section>

      <section class="panel table-panel">
        <table>
          <thead>
            <tr>
              <th>{{ t('point') }}</th>
              <th>{{ t('type') }}</th>
              <th>{{ t('flags') }}</th>
              <th>{{ t('valueBits') }}</th>
              <th>{{ t('quality') }}</th>
              <th>{{ t('timestamp') }}</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="record in states" :key="record.point_index">
              <td>{{ record.point_index }}</td>
              <td>{{ record.value_type }}</td>
              <td>{{ record.state_flags }}</td>
              <td>{{ record.value_bits }}</td>
              <td>{{ record.quality }}</td>
              <td>{{ record.timestamp_ms }}</td>
            </tr>
          </tbody>
        </table>
      </section>
    </main>
  `,
};

createApp(app).mount('#app');
