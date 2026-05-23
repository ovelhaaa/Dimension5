import { createEngine } from './audio/engine.js';

const statusEl = document.getElementById('status');
const controlsEl = document.getElementById('controls');
const btnWasm = document.getElementById('btn-wasm');
const btnAudio = document.getElementById('btn-audio');
const btnBypass = document.getElementById('btn-bypass');
const btnKillDry = document.getElementById('btn-killdry');
const btnRepeat = document.getElementById('btn-repeat');
const btnPlay = document.getElementById('btn-play');
const btnStop = document.getElementById('btn-stop');
const btnDownload = document.getElementById('btn-download');
const fileInput = document.getElementById('audio-file');
const modeEl = document.getElementById('mode');

const params = [
  ['inputGain', 0, 4, 0.01, 1], ['outputGain', 0, 4, 0.01, 1], ['dryGain', 0, 2, 0.01, 0.83],
  ['wetDirectGain', 0, 2, 0.01, 0.5], ['wetCrossGain', 0, 2, 0.01, 0.35], ['baseDelayMs', 1, 20, 0.01, 7],
  ['depthMs', 0, 6, 0.01, 0.9], ['rateHz', 0.01, 4, 0.01, 0.25], ['hpfHz', 20, 400, 1, 120],
  ['lpfHz', 2000, 12000, 1, 8000], ['analogAmount', 0, 1, 0.01, 0.35], ['companderAmount', 0, 1, 0.01, 0.35],
  ['width', 0, 2, 0.01, 1]
];

const engine = createEngine((msg) => { statusEl.textContent = `Status: ${msg}`; });

params.forEach(([name, min, max, step, value], idx) => {
  const row = document.createElement('div');
  row.className = 'row';
  row.innerHTML = `<label>${name}</label><input type="range" min="${min}" max="${max}" step="${step}" value="${value}"><span>${value}</span>`;
  const slider = row.querySelector('input');
  const valueEl = row.querySelector('span');
  slider.addEventListener('input', () => {
    valueEl.textContent = slider.value;
    engine.setParam(idx, Number(slider.value));
  });
  controlsEl.appendChild(row);
});

btnWasm.onclick = () => engine.loadWasm();
btnAudio.onclick = () => engine.startMicAudio();
btnBypass.onclick = () => {
  const enabled = engine.toggleBypass();
  btnBypass.textContent = `Bypass: ${enabled ? 'ON' : 'OFF'}`;
};
btnKillDry.onclick = () => {
  const enabled = engine.toggleKillDry();
  btnKillDry.textContent = `Kill Dry: ${enabled ? 'ON' : 'OFF'}`;
};
btnRepeat.onclick = () => {
  const enabled = engine.toggleRepeat();
  btnRepeat.textContent = `Repeat: ${enabled ? 'ON' : 'OFF'}`;
};
modeEl.onchange = () => engine.setMode(Number(modeEl.value));

fileInput.onchange = async () => {
  const file = fileInput.files?.[0];
  if (!file) return;
  await engine.loadAudioFile(file);
};

btnPlay.onclick = async () => {
  await engine.playLoadedFile(() => {
    statusEl.textContent = 'Status: file playback ended';
  });
};

btnStop.onclick = () => engine.stopPlayback();

btnDownload.onclick = () => {
  const blob = engine.getDownloadBlob();
  if (!blob) {
    statusEl.textContent = 'Status: no processed file available yet';
    return;
  }
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'dimension5-processed.webm';
  a.click();
  URL.revokeObjectURL(url);
};
