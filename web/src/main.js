import { createEngine } from './audio/engine.js';

const $ = (id) => document.getElementById(id);
const logLines = $('logLines');
const statusLed = $('statusLed');
const statusText = $('statusText');

const engine = createEngine((msg) => {
  statusText.textContent = msg;
  addLog(msg);
});

const params = [
  ['inputGain', 0, 4, 0.01, 1], ['outputGain', 0, 4, 0.01, 1], ['dryGain', 0, 2, 0.01, 0.83],
  ['wetDirectGain', 0, 2, 0.01, 0.5], ['wetCrossGain', 0, 2, 0.01, 0.35], ['baseDelayMs', 1, 20, 0.01, 7],
  ['depthMs', 0, 6, 0.01, 0.9], ['rateHz', 0.01, 4, 0.01, 0.25], ['hpfHz', 20, 400, 1, 120],
  ['lpfHz', 2000, 12000, 1, 8000], ['analogAmount', 0, 1, 0.01, 0.35], ['companderAmount', 0, 1, 0.01, 0.35], ['width', 0, 2, 0.01, 1]
];

const customContainer = $('customParams');
const fileInput = $('audio-file');
for (const [name, min, max, step, value] of params) {
  const col = document.createElement('label');
  col.className = 'param-col';
  col.innerHTML = `<span class="param-name">${name.toUpperCase()}</span><input type="range" min="${min}" max="${max}" step="${step}" value="${value}"><span class="param-val">${value}</span>`;
  const slider = col.querySelector('input');
  const valueEl = col.querySelector('.param-val');
  slider.addEventListener('input', () => {
    const v = Number(slider.value);
    valueEl.textContent = slider.value;
    engine.setParam(name, v);
  });
  engine.setParam(name, value);
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

addLog('inicializando...');
statusText.textContent = 'OFFLINE';
