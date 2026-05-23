import { createEngine } from './audio/engine.js';

const el = (id) => document.getElementById(id);
const statusEl = el('status');
const dropZone = el('drop-zone');
const fileInput = el('audio-file');
const playBtn = el('btn-play');
const stopBtn = el('btn-stop');
const repeatBtn = el('btn-repeat');
const bypassBtn = el('btn-bypass');
const renderBtn = el('btn-render');
const modeEl = el('mode');
const controls = el('controls');

const params = [
  ['inputGain', 0, 4, 0.01, 1], ['outputGain', 0, 4, 0.01, 1], ['dryGain', 0, 2, 0.01, 0.83],
  ['wetDirectGain', 0, 2, 0.01, 0.5], ['wetCrossGain', 0, 2, 0.01, 0.35], ['baseDelayMs', 1, 20, 0.01, 7],
  ['depthMs', 0, 6, 0.01, 0.9], ['rateHz', 0.01, 4, 0.01, 0.25], ['hpfHz', 20, 400, 1, 120],
  ['lpfHz', 2000, 12000, 1, 8000], ['analogAmount', 0, 1, 0.01, 0.35], ['companderAmount', 0, 1, 0.01, 0.35], ['width', 0, 2, 0.01, 1]
];

const engine = createEngine((msg) => { statusEl.textContent = msg; });

for (const [name, min, max, step, value] of params) {
  const card = document.createElement('label');
  card.className = 'control';
  card.innerHTML = `<span>${name}</span><input type="range" min="${min}" max="${max}" step="${step}" value="${value}"><strong>${value}</strong>`;
  const slider = card.querySelector('input');
  const valueEl = card.querySelector('strong');
  slider.addEventListener('input', () => {
    const v = Number(slider.value);
    valueEl.textContent = slider.value;
    engine.setParam(name, v);
  });
  engine.setParam(name, value);
  controls.appendChild(card);
}

async function boot() {
  await engine.init();
}

boot().catch((e) => { statusEl.textContent = `erro de inicialização: ${e.message}`; });

modeEl.addEventListener('change', () => engine.setMode(Number(modeEl.value)));
bypassBtn.addEventListener('click', () => {
  const state = engine.toggleBypass();
  bypassBtn.textContent = `Bypass ${state ? 'ON' : 'OFF'}`;
  bypassBtn.classList.toggle('active', state);
});
repeatBtn.addEventListener('click', () => {
  const state = engine.toggleRepeat();
  repeatBtn.textContent = `Loop ${state ? 'ON' : 'OFF'}`;
});
playBtn.addEventListener('click', async () => {
  try { await engine.playLoadedFile(); } catch (e) { statusEl.textContent = e.message; }
});
stopBtn.addEventListener('click', async () => { await engine.stopPlayback(); });
renderBtn.addEventListener('click', async () => {
  try {
    statusEl.textContent = 'renderizando offline...';
    const blob = await engine.renderOffline();
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'dimension5-offline.wav';
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 30000);
    statusEl.textContent = 'download pronto';
  } catch (e) {
    statusEl.textContent = `erro no offline render: ${e.message}`;
  }
});

async function loadFromFile(file) {
  if (!file) return;
  await engine.loadAudioFile(file);
}
fileInput.addEventListener('change', async () => loadFromFile(fileInput.files?.[0]));

dropZone.addEventListener('dragover', (e) => { e.preventDefault(); dropZone.classList.add('dragging'); });
dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragging'));
dropZone.addEventListener('drop', async (e) => {
  e.preventDefault();
  dropZone.classList.remove('dragging');
  await loadFromFile(e.dataTransfer?.files?.[0]);
});
