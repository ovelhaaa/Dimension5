import { createEngine } from './audio/engine.js';
import { PARAM_DESCRIPTORS } from './params.js';

const $ = (id) => document.getElementById(id);
const logLines = $('logLines');
const statusLed = $('statusLed');
const statusText = $('statusText');

const engine = createEngine((msg) => {
  statusText.textContent = msg;
  addLog(msg);
});

const customContainer = $('customParams');
const fileInput = $('audio-file');
for (const param of PARAM_DESCRIPTORS) {
  const col = document.createElement('label');
  col.className = 'param-col';
  col.dataset.role = param.role;
  col.innerHTML = `<span class="param-name">${param.displayName.toUpperCase()}</span><input type="range" min="${param.minValue}" max="${param.maxValue}" step="${param.step}" value="${param.defaultValue}"><span class="param-val">${formatParamValue(param, param.defaultValue)}</span>`;
  const slider = col.querySelector('input');
  const valueEl = col.querySelector('.param-val');
  slider.addEventListener('input', () => {
    const v = Number(slider.value);
    valueEl.textContent = formatParamValue(param, v);
    engine.setParam(param.stableId, v);
  });
  engine.setParam(param.stableId, param.defaultValue);
  customContainer.appendChild(col);
}

for (const [label, mode] of [['I', 0], ['II', 1], ['III', 2], ['IV', 3]]) {
  const btn = document.createElement('button');
  btn.className = 'mode-btn';
  if (mode === 0) btn.classList.add('on');
  btn.dataset.mode = String(mode);
  btn.innerHTML = '<div class="mode-led"></div><span class="mode-numeral" aria-hidden="true"></span>';
  btn.title = `Modo ${label}`;
  btn.addEventListener('click', () => {
    document.querySelectorAll('.mode-btn').forEach((b) => b.classList.remove('on'));
    btn.classList.add('on');
    $('modeSelect').value = String(mode);
    engine.setMode(mode);
    deactivateCustom();
    addLog(`modo: ${label}`);
  });
  $('modeGrid').appendChild(btn);
}

const customBtn = $('customBtn');
customBtn.addEventListener('click', () => {
  if (customBtn.classList.contains('on')) return;
  activateCustom();
  engine.setMode(4);
  addLog('modo: CUSTOM');
});

$('modeSelect').addEventListener('change', () => {
  const mode = Number($('modeSelect').value);
  engine.setMode(mode);
  if (mode === 4) {
    activateCustom();
  } else {
    deactivateCustom();
  }
  highlightGridButton(mode);
  addLog(`modo legado: ${mode === 4 ? 'CUSTOM' : mode + 1}`);
});

$('playBtn').addEventListener('click', async () => {
  try {
    await ensureReady();
    await engine.playLoadedFile();
    $('playBtn').classList.add('on');
    $('stopBtn').classList.remove('on');
    addLog('reprodução iniciada');
  } catch (e) {
    addLog(`erro: ${e.message}`);
  }
});

$('stopBtn').addEventListener('click', async () => {
  await engine.stopPlayback();
  $('stopBtn').classList.add('on');
  $('playBtn').classList.remove('on');
  addLog('parado');
});

$('loopBtn').addEventListener('click', () => {
  const state = engine.toggleRepeat();
  $('loopBtn').classList.toggle('on', state);
  addLog(`loop: ${state ? 'on' : 'off'}`);
});

$('bypassBtn').addEventListener('click', () => {
  const state = engine.toggleBypass();
  $('bypassBtn').classList.toggle('on', state);
  addLog(`bypass: ${state ? 'on' : 'off'}`);
});

$('exportBtn').addEventListener('click', async () => {
  try {
    addLog('exportando WAV (offline)…');
    const blob = await engine.renderOffline();
    if (!blob) return addLog('erro no offline render: Nenhum arquivo carregado');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'dimension5-offline.wav';
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 30000);
    addLog('download pronto');
  } catch (e) {
    addLog(`erro no offline render: ${e.message}`);
  }
});

let readyPromise = null;
function ensureReady() {
  if (!readyPromise) {
    readyPromise = (async () => {
      await engine.initFromGesture();
      statusLed.classList.add('ready');
      statusText.textContent = 'PRONTO';
    })();
  }
  return readyPromise;
}

async function loadFromFile(file) {
  if (!file) return;
  try {
    await ensureReady();
    await engine.loadAudioFile(file);
    $('dropFile').textContent = file.name;
    addLog(`arquivo: ${file.name}`);
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e);
    $('dropFile').textContent = 'falha ao carregar arquivo';
    addLog(`erro ao carregar arquivo: ${msg}`);
  }
}

fileInput.addEventListener('click', (e) => e.stopPropagation());
fileInput.addEventListener('change', async (e) => {
  await loadFromFile(e.target.files?.[0]);
});
const dropZone = $('dropZone');
dropZone.addEventListener('dragover', (e) => { e.preventDefault(); dropZone.classList.add('over'); });
dropZone.addEventListener('dragleave', () => dropZone.classList.remove('over'));
dropZone.addEventListener('drop', async (e) => { e.preventDefault(); dropZone.classList.remove('over'); await loadFromFile(e.dataTransfer?.files?.[0]); });
dropZone.addEventListener('click', (e) => {
  if (e.target === fileInput) return;
  fileInput.click();
});
dropZone.addEventListener('keydown', (e) => {
  if (e.key === 'Enter' || e.key === ' ') {
    e.preventDefault();
    fileInput.click();
  }
});

function highlightGridButton(mode) {
  document.querySelectorAll('.mode-btn').forEach((b) => {
    b.classList.toggle('on', Number(b.dataset.mode) === mode && mode !== 4);
  });
}

function activateCustom() {
  customBtn.classList.add('on');
  $('customParams').classList.add('visible');
  $('modeSelect').value = '4';
  highlightGridButton(4);
}

function deactivateCustom() {
  customBtn.classList.remove('on');
  $('customParams').classList.remove('visible');
}

function addLog(msg) {
  const row = document.createElement('div');
  row.className = 'log-line';
  row.textContent = msg;
  logLines.appendChild(row);
  logLines.scrollTop = logLines.scrollHeight;
}

function formatParamValue(param, value) {
  const decimals = param.step < 0.1 ? 2 : 0;
  const text = Number(value).toFixed(decimals);
  return param.unit ? `${text} ${param.unit}` : text;
}

addLog('inicializando...');
statusText.textContent = 'OFFLINE';
