import { createApp, computed, onMounted, ref, watch } from '../vendor/vue.esm-browser.prod.js';

const languages = [
  { value: 'uk', label: 'English (UK)', htmlLang: 'en-GB' },
  { value: 'fr', label: 'Francais', htmlLang: 'fr' },
  { value: 'de', label: 'Deutsch', htmlLang: 'de' },
  { value: 'es', label: 'Espanol', htmlLang: 'es' },
  { value: 'zh-Hans', label: 'JianTi ZhongWen', htmlLang: 'zh-Hans' },
];

const translations = {
  uk: {
    title: 'NodeNet PLC',
    eyebrow: 'NodeNet PLC Console',
    heroTitle: 'Local PLC application',
    heroLead: 'The ESP32 serves the app locally, keeps the startup portal separate, and mirrors PLC slot states from the FPGA snapshot.',
    menuHome: 'Home',
    menuWifiSetup: 'Wi-Fi setup',
    menuAbout: 'About',
    language: 'Language',
    saveSettings: 'Save settings',
    settingsSaved: 'Settings saved.',
    settingsSaveFailed: 'Unable to save settings:',
    loading: 'loading',
    accessTitle: 'Access and language',
    accessLead: 'These settings are persisted in ESP32 flash.',
    anonymousLabel: 'Anonymous access',
    anonymousHint: 'When enabled, the app bypasses the fake login flow. When disabled, the hard-coded admin account is required.',
    loginTitle: 'Admin sign-in',
    loginLead: 'This is a temporary fake user flow until flash-backed users, passwords and groups are implemented.',
    authRequired: 'Anonymous access is disabled. Sign in to open the app.',
    username: 'Username',
    password: 'Password',
    signIn: 'Sign in',
    signOut: 'Sign out',
    signedInAs: 'Signed in as',
    adminCredentialsHint: 'Temporary hard-coded account: admin / admin',
    authFailed: 'Unable to sign in:',
    sessionExpired: 'Authentication is required again.',
    anonymousMode: 'Anonymous mode',
    adminMode: 'Admin mode',
    homeTitle: 'PLC slot states',
    homeLead: 'Live slot states are derived from the current snapshot using the same state mapping as the LVGL home screen.',
    slotRunning: 'Running',
    slotFaulted: 'Faulted',
    slotStopped: 'Stopped',
    slotLoaded: 'Loaded',
    slotEmpty: 'Empty',
    slotUnknown: 'Unknown',
    slotSourceLive: 'live state',
    slotSourceFallback: 'fallback state',
    slotSourceMissing: 'no state data',
    wifiSetupTitle: 'Wi-Fi setup',
    wifiSetupLead: 'Use the same onboard APIs as the startup portal without leaving the application shell.',
    currentNetwork: 'Current network',
    setupVia: 'setup via',
    connectionInProgress: 'connection in progress',
    ssid: 'SSID',
    passwordPlaceholder: 'Password',
    browse: 'Browse',
    selectNetwork: 'Select scanned network',
    connect: 'Save and connect',
    disconnect: 'Disconnect',
    forget: 'Forget',
    wifiUpdated: 'Wi-Fi settings updated.',
    wifiActionFailed: 'Wi-Fi action failed:',
    openStartupPortal: 'Open startup portal',
    openStartupPortalHint: 'The startup portal remains the public setup surface.',
    adminSettingsTitle: 'Admin-only app settings',
    settingsRequireAdmin: 'Sign in as admin to edit app access settings from inside the app.',
    aboutTitle: 'About this app',
    aboutLead: 'Runtime transport stays local: HTTP for the app, WebSocket for change notifications, SPI for FPGA mailbox updates.',
    boot: 'Boot',
    snapshotLoaded: 'snapshot loaded',
    snapshotLoading: 'loading snapshot',
    wifi: 'Wi-Fi',
    socket: 'Socket',
    sequence: 'Sequence',
    plcSnapshot: 'PLC snapshot',
    pointsLoaded: 'points loaded',
    refreshSnapshot: 'Request snapshot refresh',
    slotStart: 'Start',
    slotStop: 'Stop',
    slotReset: 'Reset',
    slotClearFault: 'Clear fault',
    slotActionFailed: 'PLC slot action failed:',
    point: 'Point',
    type: 'Type',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Timestamp',
  },
  fr: {
    title: 'NodeNet PLC',
    eyebrow: 'Console PLC NodeNet',
    heroTitle: 'Application PLC locale',
    heroLead: 'L\'ESP32 sert l\'application localement, garde le portail startup separe, et reflete les etats de slots PLC depuis le snapshot FPGA.',
    menuHome: 'Home',
    menuWifiSetup: 'Wi-Fi setup',
    menuAbout: 'About',
    language: 'Langue',
    saveSettings: 'Enregistrer les parametres',
    settingsSaved: 'Parametres enregistres.',
    settingsSaveFailed: 'Impossible d\'enregistrer les parametres :',
    loading: 'chargement',
    accessTitle: 'Acces et langue',
    accessLead: 'Ces parametres sont persistants dans la flash ESP32.',
    anonymousLabel: 'Acces anonyme',
    anonymousHint: 'Quand il est active, l\'application contourne le faux login. Quand il est desactive, le compte admin durci devient obligatoire.',
    loginTitle: 'Connexion admin',
    loginLead: 'Ceci est un flux utilisateur fake temporaire en attendant de vrais users, mots de passe et groupes stockes en flash.',
    authRequired: 'L\'acces anonyme est desactive. Connecte-toi pour ouvrir l\'application.',
    username: 'Utilisateur',
    password: 'Mot de passe',
    signIn: 'Se connecter',
    signOut: 'Se deconnecter',
    signedInAs: 'Connecte en tant que',
    adminCredentialsHint: 'Compte temporaire durci : admin / admin',
    authFailed: 'Connexion impossible :',
    sessionExpired: 'Une authentification est de nouveau requise.',
    anonymousMode: 'Mode anonyme',
    adminMode: 'Mode admin',
    homeTitle: 'Etat des slots PLC',
    homeLead: 'Les etats des slots sont derives du snapshot courant avec la meme logique que l\'ecran LVGL.',
    slotRunning: 'Running',
    slotFaulted: 'Faulted',
    slotStopped: 'Stopped',
    slotLoaded: 'Loaded',
    slotEmpty: 'Empty',
    slotUnknown: 'Unknown',
    slotSourceLive: 'etat live',
    slotSourceFallback: 'etat fallback',
    slotSourceMissing: 'aucune donnee d\'etat',
    wifiSetupTitle: 'Configuration Wi-Fi',
    wifiSetupLead: 'Cette vue reutilise les memes API embarquees que le portail startup sans quitter l\'application.',
    currentNetwork: 'Reseau courant',
    setupVia: 'setup via',
    connectionInProgress: 'connexion en cours',
    ssid: 'SSID',
    passwordPlaceholder: 'Mot de passe',
    browse: 'Parcourir',
    selectNetwork: 'Choisir un reseau scanne',
    connect: 'Enregistrer et connecter',
    disconnect: 'Deconnecter',
    forget: 'Forget',
    wifiUpdated: 'Parametres Wi-Fi mis a jour.',
    wifiActionFailed: 'Echec action Wi-Fi :',
    openStartupPortal: 'Ouvrir le portail startup',
    openStartupPortalHint: 'Le portail startup reste la surface publique de configuration.',
    adminSettingsTitle: 'Parametres app reserves admin',
    settingsRequireAdmin: 'Connecte-toi en admin pour modifier l\'acces de l\'application depuis cette interface.',
    aboutTitle: 'A propos de l\'application',
    aboutLead: 'Le transport runtime reste local : HTTP pour l\'app, WebSocket pour les notifications de changement, SPI pour la mailbox FPGA.',
    boot: 'Boot',
    snapshotLoaded: 'snapshot charge',
    snapshotLoading: 'chargement snapshot',
    wifi: 'Wi-Fi',
    socket: 'Socket',
    sequence: 'Sequence',
    plcSnapshot: 'Snapshot PLC',
    pointsLoaded: 'points charges',
    refreshSnapshot: 'Relancer un snapshot',
    slotStart: 'Start',
    slotStop: 'Stop',
    slotReset: 'Reset',
    slotClearFault: 'Clear Fault',
    slotActionFailed: 'Echec action slot PLC :',
    point: 'Point',
    type: 'Type',
    flags: 'Flags',
    valueBits: 'ValueBits',
    quality: 'Quality',
    timestamp: 'Timestamp',
  },
  de: {},
  es: {},
  'zh-Hans': {},
};

const socketState = ref('offline');
const systemInfo = ref(null);
const states = ref([]);
const errorText = ref('');
const settingsMessage = ref('');
const wifiMessage = ref('');
const authMessage = ref('');
const lastSequence = ref(null);
const currentLanguage = ref('uk');
const anonymousAccess = ref(true);
const currentView = ref('home');
const authState = ref({
  anonymous_access: true,
  login_required: false,
  authenticated: true,
  can_admin: false,
  user: { username: 'anonymous', group: 'public', permissions: [] },
});
const loginUsername = ref('admin');
const loginPassword = ref('admin');
const wifiSsid = ref('');
const wifiPassword = ref('');
const wifiScanResults = ref([]);
const selectedScanSsid = ref('');
const slotActionKey = ref('');

let socket = null;
let socketReconnectEnabled = false;

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
}

function syncAuth(auth) {
  if (!auth) {
    return;
  }

  authState.value = {
    anonymous_access: !!auth.anonymous_access,
    login_required: !!auth.login_required,
    authenticated: !!auth.authenticated,
    can_admin: !!auth.can_admin,
    user: auth.user || (auth.anonymous_access
      ? { username: 'anonymous', group: 'public', permissions: [] }
      : null),
  };
}

function disconnectSocket() {
  socketReconnectEnabled = false;
  if (socket) {
    const activeSocket = socket;
    socket = null;
    activeSocket.close();
  }
  socketState.value = 'offline';
}

async function fetchJson(url, options) {
  const response = await fetch(url, options);
  const payload = await response.json();
  if (!response.ok) {
    const error = new Error(payload.error || `HTTP ${response.status}`);
    error.status = response.status;
    throw error;
  }
  return payload;
}

async function loadSettings() {
  const payload = await fetchJson('/api/settings');
  syncSettings(payload.settings || payload);
}

async function loadAuthStatus() {
  const payload = await fetchJson('/api/auth/status');
  syncAuth(payload.auth || payload);
}

async function handleProtectedFailure(error) {
  if (error?.status !== 401) {
    throw error;
  }

  disconnectSocket();
  states.value = [];
  systemInfo.value = null;
  lastSequence.value = null;
  await loadAuthStatus();
  authMessage.value = t('sessionExpired');
}

async function loadSystemInfo() {
  try {
    const payload = await fetchJson('/api/system/info');
    systemInfo.value = payload;
    syncSettings(payload.settings);
  } catch (error) {
    await handleProtectedFailure(error);
    throw error;
  }
}

async function loadStates() {
  try {
    const snapshot = await fetchJson('/api/plc/states/snapshot');
    states.value = snapshot.records || [];
    lastSequence.value = snapshot.sequence;
    if (systemInfo.value) {
      systemInfo.value = {
        ...systemInfo.value,
        snapshot: {
          ...(systemInfo.value.snapshot || {}),
          sequence: snapshot.sequence,
          point_count: snapshot.point_count,
          loaded_points: snapshot.loaded_points,
          complete: snapshot.complete,
        },
      };
    }
  } catch (error) {
    await handleProtectedFailure(error);
    throw error;
  }
}

async function loadProtectedData() {
  await Promise.all([loadSystemInfo(), loadStates()]);
}

function updateWifiInSystemInfo(payload) {
  const wifi = payload?.wifi || payload;
  if (!wifi) {
    return;
  }

  if (!systemInfo.value) {
    systemInfo.value = { wifi };
  } else {
    systemInfo.value = { ...systemInfo.value, wifi };
  }

  if (!wifiSsid.value && wifi.sta_ssid) {
    wifiSsid.value = wifi.sta_ssid;
  }
}

async function requestRefresh() {
  try {
    await fetchJson('/api/plc/states/refresh', { method: 'POST' });
  } catch (error) {
    await handleProtectedFailure(error);
    throw error;
  }
}

async function writePlcPoint(record, valueBits, writeFlags = 1) {
  if (!record) {
    throw new Error('missing point record');
  }

  await fetchJson('/api/plc/write', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      point_index: Number(record.point_index),
      value_type: Number(record.value_type),
      value_bits: Number(valueBits) >>> 0,
      write_flags: Number(writeFlags) >>> 0,
    }),
  });
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

  const settings = payload.settings || payload;
  if (systemInfo.value) {
    systemInfo.value = { ...systemInfo.value, settings };
  }
  syncSettings(settings);
  settingsMessage.value = t('settingsSaved');
  await loadAuthStatus();
}

async function loginAdmin() {
  const payload = await fetchJson('/api/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      username: loginUsername.value,
      password: loginPassword.value,
    }),
  });

  syncAuth(payload.auth || payload);
  authMessage.value = '';
  await loadProtectedData();
  connectSocket();
}

async function logoutAdmin() {
  const payload = await fetchJson('/api/auth/logout', { method: 'POST' });
  syncAuth(payload.auth || payload);
  disconnectSocket();
  lastSequence.value = null;
  if (!authState.value.anonymous_access) {
    states.value = [];
    systemInfo.value = null;
  }
}

function upsertStateRecord(record) {
  if (!record || record.point_index === undefined || record.point_index === null) {
    return;
  }

  const pointIndex = Number(record.point_index);
  const nextStates = [...states.value];
  const existingIndex = nextStates.findIndex((entry) => Number(entry?.point_index) === pointIndex);
  if (existingIndex >= 0) {
    nextStates[existingIndex] = record;
  } else {
    nextStates.push(record);
    nextStates.sort((left, right) => Number(left?.point_index || 0) - Number(right?.point_index || 0));
  }
  states.value = nextStates;
}

function applySnapshotMeta(snapshot) {
  if (!snapshot) {
    return;
  }

  lastSequence.value = snapshot.sequence ?? lastSequence.value;
  if (!systemInfo.value) {
    return;
  }

  systemInfo.value = {
    ...systemInfo.value,
    snapshot: {
      ...(systemInfo.value.snapshot || {}),
      ...snapshot,
    },
  };
}

function slotActionBusy(slotId, action) {
  return slotActionKey.value === `${slotId}:${action}`;
}

function connectSocket() {
  if (!authState.value.authenticated || socket) {
    return;
  }

  const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${protocol}://${window.location.host}/ws`);
  socketReconnectEnabled = true;
  socketState.value = 'connecting';

  socket.addEventListener('open', () => {
    socketState.value = 'online';
  });

  socket.addEventListener('close', () => {
    socket = null;
    socketState.value = 'offline';
    if (socketReconnectEnabled && authState.value.authenticated) {
      window.setTimeout(connectSocket, 1500);
    }
  });

  socket.addEventListener('error', () => {
    socketState.value = 'error';
  });

  socket.addEventListener('message', async (event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'boot_progress') {
      if (systemInfo.value) {
        systemInfo.value = { ...systemInfo.value, boot: message.boot };
      }
      return;
    }

    if (message.type === 'wifi_status') {
      updateWifiInSystemInfo(message.wifi);
      return;
    }

    if (message.type === 'settings') {
      if (systemInfo.value) {
        systemInfo.value = { ...systemInfo.value, settings: message.settings };
      }
      syncSettings(message.settings);
      await loadAuthStatus();
      return;
    }

    if (message.type === 'plc_snapshot_available') {
      applySnapshotMeta(message.snapshot);
      if ((message.snapshot?.loaded_points || 0) < states.value.length) {
        states.value = [];
      }
      return;
    }

    if (message.type === 'plc_point_update') {
      applySnapshotMeta(message.snapshot);
      upsertStateRecord(message.record);
      return;
    }
  });
}

function slotStateFromText(value) {
  if (!value) {
    return '?';
  }
  if (value === 'running') {
    return 'R';
  }
  if (value === 'faulted') {
    return 'F';
  }
  if (value === 'stopped') {
    return 'S';
  }
  if (value === 'loaded') {
    return 'L';
  }
  if (value === 'empty') {
    return '-';
  }
  return '?';
}

function slotStateFromFallback(loaded, runEnabled, haveStatus, statusBits) {
  if (!loaded) {
    return '-';
  }
  if (haveStatus && ((statusBits & 0x80000000) !== 0)) {
    return 'F';
  }
  if (runEnabled || (haveStatus && statusBits === 2)) {
    return 'R';
  }
  return 'L';
}

function slotHexDigit(slotId) {
  return slotId < 10 ? String(slotId) : String.fromCharCode('A'.charCodeAt(0) + (slotId - 10));
}

function stateLabelKey(code) {
  if (code === 'R') {
    return 'slotRunning';
  }
  if (code === 'F') {
    return 'slotFaulted';
  }
  if (code === 'S') {
    return 'slotStopped';
  }
  if (code === 'L') {
    return 'slotLoaded';
  }
  if (code === '-') {
    return 'slotEmpty';
  }
  return 'slotUnknown';
}

function stateClass(code) {
  if (code === 'R') {
    return 'running';
  }
  if (code === 'F') {
    return 'faulted';
  }
  if (code === 'S') {
    return 'stopped';
  }
  if (code === 'L') {
    return 'loaded';
  }
  if (code === '-') {
    return 'empty';
  }
  return 'unknown';
}

function boolFromRecord(record) {
  return !!record && Number(record.value_bits) !== 0;
}

function uintFromRecord(record) {
  return record ? (Number(record.value_bits) >>> 0) : 0;
}

const app = {
  setup() {
    const pointCount = computed(() => systemInfo.value?.snapshot?.point_count || 0);
    const loadedPoints = computed(() => systemInfo.value?.snapshot?.loaded_points || 0);
    const isAuthenticated = computed(() => authState.value.authenticated);
    const canAdmin = computed(() => authState.value.can_admin);
    const authUserName = computed(() => authState.value.user?.username || '');
    const wifiSummary = computed(() => {
      const wifi = systemInfo.value?.wifi;
      if (!wifi) {
        return t('loading');
      }
      if (wifi.sta_connected) {
        return `${wifi.sta_ssid} (${wifi.sta_ip})`;
      }
      if (wifi.sta_has_credentials) {
        return `${wifi.sta_ssid} (${t('connectionInProgress')})`;
      }
      return `${t('setupVia')} ${wifi.ap_ssid}`;
    });

    const slots = computed(() => {
      const byFeature = new Map();
      for (const record of states.value) {
        if (!record?.feature || !record?.point_id) {
          continue;
        }
        if (!byFeature.has(record.feature)) {
          byFeature.set(record.feature, {});
        }
        byFeature.get(record.feature)[record.point_id] = record;
      }

      const nextSlots = [];
      for (let index = 0; index < 16; index += 1) {
        const feature = `plc.slot${index}`;
        const slotRecords = byFeature.get(feature) || {};
        const liveState = slotRecords.state?.string_value || '';
        const loadedRecord = slotRecords.loaded;
        const runEnabledRecord = slotRecords.runEnabled;
        const statusRecord = slotRecords.status;
        const startRecord = slotRecords.start;
        const stopRecord = slotRecords.stop;
        const resetRecord = slotRecords.reset;
        const clearFaultRecord = slotRecords.clearFault;

        let stateCode = '?';
        let sourceLabel = t('slotSourceMissing');
        if (liveState) {
          stateCode = slotStateFromText(liveState);
          sourceLabel = t('slotSourceLive');
        } else if (loadedRecord || runEnabledRecord || statusRecord) {
          stateCode = slotStateFromFallback(
            boolFromRecord(loadedRecord),
            boolFromRecord(runEnabledRecord),
            !!statusRecord,
            uintFromRecord(statusRecord),
          );
          sourceLabel = t('slotSourceFallback');
        }

        nextSlots.push({
          id: index,
          hexId: slotHexDigit(index),
          stateCode,
          stateLabel: t(stateLabelKey(stateCode)),
          stateClass: stateClass(stateCode),
          sourceLabel,
          canStart: stateCode === 'S' && !!startRecord,
          canStop: stateCode === 'R' && !!stopRecord,
          canReset: (stateCode === 'R' || stateCode === 'S' || stateCode === 'L' || stateCode === 'F') && !!resetRecord,
          canClearFault: stateCode === 'F' && !!clearFaultRecord,
          startRecord,
          stopRecord,
          resetRecord,
          clearFaultRecord,
        });
      }
      return nextSlots;
    });

    watch(currentLanguage, () => {
      applyDocumentLanguage();
    }, { immediate: true });

    onMounted(async () => {
      try {
        await Promise.all([loadSettings(), loadAuthStatus()]);
        if (authState.value.authenticated) {
          await loadProtectedData();
          connectSocket();
        }
      } catch (error) {
        errorText.value = error.message;
      }
    });

    return {
      anonymousAccess,
      authMessage,
      authState,
      authUserName,
      canAdmin,
      currentLanguage,
      currentView,
      errorText,
      isAuthenticated,
      languages,
      loadedPoints,
      loginPassword,
      loginUsername,
      pointCount,
      settingsMessage,
      socketState,
      slotActionBusy,
      slots,
      states,
      systemInfo,
      t,
      wifiMessage,
      wifiPassword,
      wifiScanResults,
      wifiSsid,
      wifiSummary,
      selectedScanSsid,
      browseWifiNetworks: async () => {
        try {
          const payload = await fetchJson('/api/wifi/scan');
          wifiScanResults.value = payload.networks || [];
          if (wifiScanResults.value.length > 0 && !selectedScanSsid.value) {
            selectedScanSsid.value = wifiScanResults.value[0].ssid;
          }
        } catch (error) {
          wifiMessage.value = `${t('wifiActionFailed')} ${error.message}`;
        }
      },
      changeSelectedNetwork: () => {
        if (selectedScanSsid.value) {
          wifiSsid.value = selectedScanSsid.value;
        }
      },
      loginAdmin: async () => {
        try {
          await loginAdmin();
        } catch (error) {
          authMessage.value = `${t('authFailed')} ${error.message}`;
        }
      },
      logoutAdmin: async () => {
        try {
          await logoutAdmin();
        } catch (error) {
          authMessage.value = error.message;
        }
      },
      requestRefresh: async () => {
        try {
          await requestRefresh();
        } catch (error) {
          errorText.value = error.message;
        }
      },
      triggerSlotAction: async (slot, action) => {
        const record = slot?.[`${action}Record`];
        if (!record) {
          return;
        }

        slotActionKey.value = `${slot.id}:${action}`;
        try {
          await writePlcPoint(record, 1, 1);
        } catch (error) {
          errorText.value = `${t('slotActionFailed')} ${error.message}`;
        } finally {
          slotActionKey.value = '';
        }
      },
      saveSettings: async () => {
        try {
          await saveSettings();
        } catch (error) {
          settingsMessage.value = `${t('settingsSaveFailed')} ${error.message}`;
        }
      },
      saveWifiCredentials: async () => {
        try {
          const payload = await fetchJson('/api/wifi/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid: wifiSsid.value, password: wifiPassword.value }),
          });
          updateWifiInSystemInfo(payload);
          wifiMessage.value = t('wifiUpdated');
        } catch (error) {
          wifiMessage.value = `${t('wifiActionFailed')} ${error.message}`;
        }
      },
      disconnectWifi: async () => {
        try {
          const payload = await fetchJson('/api/wifi/disconnect', { method: 'POST' });
          updateWifiInSystemInfo(payload);
          wifiMessage.value = t('wifiUpdated');
        } catch (error) {
          wifiMessage.value = `${t('wifiActionFailed')} ${error.message}`;
        }
      },
      forgetWifi: async () => {
        try {
          const payload = await fetchJson('/api/wifi/forget', { method: 'POST' });
          updateWifiInSystemInfo(payload);
          wifiMessage.value = t('wifiUpdated');
        } catch (error) {
          wifiMessage.value = `${t('wifiActionFailed')} ${error.message}`;
        }
      },
    };
  },
  template: `
    <main class="layout">
      <section class="hero panel">
        <div class="hero-top">
          <div>
            <p class="eyebrow">{{ t('eyebrow') }}</p>
            <h1>{{ t('heroTitle') }}</h1>
            <p class="lead">{{ t('heroLead') }}</p>
          </div>
          <div class="identity-card">
            <span class="identity-mode">{{ authState.can_admin ? t('adminMode') : t('anonymousMode') }}</span>
            <strong v-if="authUserName">{{ t('signedInAs') }} {{ authUserName }}</strong>
            <strong v-else>{{ t('authRequired') }}</strong>
            <button v-if="authState.can_admin" class="secondary-button" @click="logoutAdmin">{{ t('signOut') }}</button>
          </div>
        </div>
      </section>

      <section class="panel login-panel" v-if="!isAuthenticated">
        <div>
          <h2>{{ t('loginTitle') }}</h2>
          <p class="lead">{{ t('loginLead') }}</p>
          <p class="hint">{{ t('adminCredentialsHint') }}</p>
        </div>
        <div class="login-form">
          <label class="field">
            <span>{{ t('username') }}</span>
            <input v-model="loginUsername" type="text" autocomplete="username">
          </label>
          <label class="field">
            <span>{{ t('password') }}</span>
            <input v-model="loginPassword" type="password" autocomplete="current-password">
          </label>
          <button @click="loginAdmin">{{ t('signIn') }}</button>
          <a class="button-link secondary-button" href="/startup/">{{ t('openStartupPortal') }}</a>
        </div>
      </section>

      <section class="panel message-panel error" v-if="authMessage && !isAuthenticated">
        {{ authMessage }}
      </section>

      <template v-if="isAuthenticated">
        <nav class="menu panel">
          <button :class="['menu-button', { active: currentView === 'home' }]" @click="currentView = 'home'">{{ t('menuHome') }}</button>
          <button :class="['menu-button', { active: currentView === 'wifi' }]" @click="currentView = 'wifi'">{{ t('menuWifiSetup') }}</button>
          <button :class="['menu-button', { active: currentView === 'about' }]" @click="currentView = 'about'">{{ t('menuAbout') }}</button>
        </nav>

        <section class="panel page-section" v-if="currentView === 'home'">
          <div class="section-head">
            <div>
              <h2>{{ t('homeTitle') }}</h2>
              <p class="lead">{{ t('homeLead') }}</p>
            </div>
            <button @click="requestRefresh">{{ t('refreshSnapshot') }}</button>
          </div>
          <div class="slot-grid">
            <article v-for="slot in slots" :key="slot.id" :class="['slot-card', slot.stateClass]">
              <div class="slot-card-top">
                <span class="slot-id">{{ slot.hexId }}</span>
                <span class="slot-glyph">{{ slot.stateCode }}</span>
              </div>
              <strong>{{ slot.stateLabel }}</strong>
              <small>{{ slot.sourceLabel }}</small>
              <div class="slot-actions" v-if="slot.canStart || slot.canStop || slot.canReset || slot.canClearFault">
                <button v-if="slot.canStart" class="slot-action-button" :disabled="slotActionBusy(slot.id, 'start')" @click="triggerSlotAction(slot, 'start')">{{ t('slotStart') }}</button>
                <button v-if="slot.canStop" class="slot-action-button secondary-button" :disabled="slotActionBusy(slot.id, 'stop')" @click="triggerSlotAction(slot, 'stop')">{{ t('slotStop') }}</button>
                <button v-if="slot.canReset" class="slot-action-button secondary-button" :disabled="slotActionBusy(slot.id, 'reset')" @click="triggerSlotAction(slot, 'reset')">{{ t('slotReset') }}</button>
                <button v-if="slot.canClearFault" class="slot-action-button danger-button" :disabled="slotActionBusy(slot.id, 'clearFault')" @click="triggerSlotAction(slot, 'clearFault')">{{ t('slotClearFault') }}</button>
              </div>
            </article>
          </div>
        </section>

        <section class="page-grid" v-if="currentView === 'wifi'">
          <article class="panel page-section">
            <div class="section-head compact">
              <div>
                <h2>{{ t('wifiSetupTitle') }}</h2>
                <p class="lead">{{ t('wifiSetupLead') }}</p>
              </div>
              <a class="button-link secondary-button" href="/startup/">{{ t('openStartupPortal') }}</a>
            </div>

            <div class="info-strip" v-if="systemInfo?.wifi">
              <span>{{ t('currentNetwork') }}</span>
              <strong>{{ wifiSummary }}</strong>
              <small>AP {{ systemInfo.wifi.ap_ssid }} / {{ systemInfo.wifi.ap_ip }}</small>
            </div>

            <div class="settings-grid">
              <label class="field">
                <span>{{ t('ssid') }}</span>
                <input v-model="wifiSsid" type="text" autocomplete="off">
              </label>
              <label class="field">
                <span>{{ t('password') }}</span>
                <input v-model="wifiPassword" type="password" :placeholder="t('passwordPlaceholder')" autocomplete="new-password">
              </label>
              <label class="field">
                <span>{{ t('selectNetwork') }}</span>
                <select v-model="selectedScanSsid" @change="changeSelectedNetwork">
                  <option value="">{{ t('selectNetwork') }}</option>
                  <option v-for="network in wifiScanResults" :key="network.ssid" :value="network.ssid">
                    {{ network.ssid }} ({{ network.rssi }} dBm)
                  </option>
                </select>
              </label>
              <div class="button-row">
                <button @click="browseWifiNetworks">{{ t('browse') }}</button>
                <button @click="saveWifiCredentials">{{ t('connect') }}</button>
                <button class="secondary-button" @click="disconnectWifi">{{ t('disconnect') }}</button>
                <button class="secondary-button" @click="forgetWifi">{{ t('forget') }}</button>
              </div>
            </div>

            <p class="hint">{{ t('openStartupPortalHint') }}</p>
          </article>

          <article class="panel page-section">
            <div class="section-head compact">
              <div>
                <h2>{{ t('adminSettingsTitle') }}</h2>
                <p class="lead">{{ t('accessLead') }}</p>
              </div>
            </div>

            <div v-if="canAdmin" class="settings-grid">
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

            <div v-else class="admin-lock">
              <p>{{ t('settingsRequireAdmin') }}</p>
              <div class="login-form inline-login">
                <label class="field">
                  <span>{{ t('username') }}</span>
                  <input v-model="loginUsername" type="text" autocomplete="username">
                </label>
                <label class="field">
                  <span>{{ t('password') }}</span>
                  <input v-model="loginPassword" type="password" autocomplete="current-password">
                </label>
                <button @click="loginAdmin">{{ t('signIn') }}</button>
              </div>
              <p class="hint">{{ t('adminCredentialsHint') }}</p>
            </div>
          </article>
        </section>

        <section class="page-grid" v-if="currentView === 'about'">
          <article class="panel page-section about-main">
            <div class="section-head compact">
              <div>
                <h2>{{ t('aboutTitle') }}</h2>
                <p class="lead">{{ t('aboutLead') }}</p>
              </div>
            </div>

            <div class="grid" v-if="systemInfo">
              <article class="metric-card">
                <h3>{{ t('boot') }}</h3>
                <strong>{{ systemInfo.boot.percent }}%</strong>
                <span>{{ systemInfo.boot.snapshot_complete ? t('snapshotLoaded') : t('snapshotLoading') }}</span>
              </article>
              <article class="metric-card">
                <h3>{{ t('wifi') }}</h3>
                <strong>{{ wifiSummary }}</strong>
                <span>AP {{ systemInfo.wifi.ap_ssid }} / {{ systemInfo.wifi.ap_ip }}</span>
              </article>
              <article class="metric-card">
                <h3>{{ t('socket') }}</h3>
                <strong>{{ socketState }}</strong>
                <span>{{ t('sequence') }} {{ systemInfo.snapshot.sequence }}</span>
              </article>
              <article class="metric-card wide">
                <h3>{{ t('plcSnapshot') }}</h3>
                <strong>{{ loadedPoints }} / {{ pointCount }}</strong>
                <span>{{ t('pointsLoaded') }}</span>
              </article>
            </div>
          </article>

          <article class="panel table-panel">
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
                  <td>{{ record.feature ? record.feature + '.' + record.point_id : record.point_index }}</td>
                  <td>{{ record.value_type }}</td>
                  <td>{{ record.state_flags }}</td>
                  <td>{{ record.string_value || record.value_bits }}</td>
                  <td>{{ record.quality }}</td>
                  <td>{{ record.timestamp_ms }}</td>
                </tr>
              </tbody>
            </table>
          </article>
        </section>

        <section class="panel message-panel" v-if="settingsMessage">
          {{ settingsMessage }}
        </section>

        <section class="panel message-panel" v-if="wifiMessage">
          {{ wifiMessage }}
        </section>

        <section class="panel message-panel error" v-if="errorText">
          {{ errorText }}
        </section>
      </template>
    </main>
  `,
};

createApp(app).mount('#app');
