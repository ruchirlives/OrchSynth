const canvas = document.querySelector('#canvas');
const canvasShell = document.querySelector('#canvas-shell');
const overlay = document.querySelector('#overlay');
const skin = document.querySelector('#skin');
const status = document.querySelector('#status');
const selectionName = document.querySelector('#selection-name');
const selectionParent = document.querySelector('#selection-parent');
const sizeReadout = document.querySelector('#size-readout');
const fields = Object.fromEntries(['x', 'y', 'w', 'h'].map(key => [key, document.querySelector(`#field-${key}`)]));
const fontSizeField = document.querySelector('#field-fontSize');
const fontSizeLabel = document.querySelector('#font-size-label');
const state = { layout: null, selected: [], showGroups: false, showNames: false, snap: 2, zoom: 1.25, history: [], future: [], interaction: null };

const clone = value => JSON.parse(JSON.stringify(value));
const entries = () => [
  ...Object.entries(state.layout.elements).map(([name, item]) => ({ name, item, group: false })),
  ...(state.showGroups ? Object.entries(state.layout.groups).map(([name, item]) => ({ name, item, group: true })) : [])
].sort((left, right) => (left.item.z || 0) - (right.item.z || 0));

function absoluteRect(item) {
  return { x: item.x, y: item.y, w: item.w, h: item.h };
}

function selectedEntry() {
  return state.selected.length === 1 ? entries().find(entry => entry.name === state.selected[0]) || null : null;
}

function isSelected(name) { return state.selected.includes(name); }

function snapshot() {
  state.history.push(clone(state.layout));
  if (state.history.length > 40) state.history.shift();
  state.future = [];
  updateActions();
}

function updateActions() {
  document.querySelector('#undo').disabled = !state.history.length;
  document.querySelector('#redo').disabled = !state.future.length;
}

function setStatus(message, error = false) {
  status.textContent = message;
  status.style.color = error ? '#f2a05b' : '#a4ed66';
}

function select(name, additive = false) {
  if (additive) {
    state.selected = isSelected(name) ? state.selected.filter(item => item !== name) : [...state.selected, name];
  } else {
    state.selected = [name];
  }
  render();
}

function updateInspector() {
  const entry = selectedEntry();
  const disabled = !entry;
  for (const input of Object.values(fields)) input.disabled = disabled;
  const supportsFontSize = Boolean(entry && entry.item.type === 'control');
  fontSizeField.disabled = !supportsFontSize;
  fontSizeLabel.hidden = !supportsFontSize;
  if (!entry) {
    selectionName.textContent = 'None';
    sizeReadout.textContent = state.selected.length > 1 ? `${state.selected.length} objects selected.` : 'Select a control to resize it.';
    selectionParent.textContent = 'Select a control or container.';
    return;
  }
  selectionName.textContent = entry.item.label || entry.name;
  sizeReadout.textContent = supportsFontSize
    ? `${entry.item.w} x ${entry.item.h} px / font ${entry.item.fontSize || 12} px`
    : `${entry.item.w} x ${entry.item.h} px`;
  selectionParent.textContent = `${entry.group ? 'Container' : 'Control'} / canvas coordinates / parent: ${entry.item.parent || 'canvas'}`;
  for (const key of Object.keys(fields)) fields[key].value = entry.item[key];
  if (supportsFontSize) fontSizeField.value = entry.item.fontSize || 12;
}

function previewFor(entry) {
  const preview = document.createElement('div');
  preview.className = `preview ${entry.item.type || ''}`;
  if (entry.item.type === 'play') preview.innerHTML = '<img src="/asset/orch_button_play_fit.png" alt="">';
  else if (entry.item.type === 'dial') {
    preview.innerHTML = '<img src="/asset/orch_knob_square.png" alt="">';
  } else if (entry.item.type === 'scope') {
    preview.innerHTML = '<div class="scope-screen"><span class="scope-heading">CURRENT PATCH</span><span class="scope-patch">Default Poly Sine</span></div>';
  } else if (entry.item.type === 'waveform') {
    preview.innerHTML = '<svg class="animated-waveform" viewBox="0 0 187 37" preserveAspectRatio="none" aria-hidden="true"><path d="M0 18 C5 3 10 3 15 18 S25 33 30 18 S40 3 45 18 S55 33 60 18 S70 3 75 18 S85 33 90 18 S100 3 105 18 S115 33 120 18 S130 3 135 18 S145 33 150 18 S160 3 165 18 S175 33 180 18 S185 3 187 18"/></svg>';
  } else if (entry.item.type === 'panel') {
    preview.classList.add('panel-preview');
    if (entry.item.asset) preview.innerHTML = `<img src="/asset/${entry.item.asset}" alt="">`;
  } else {
    const text = document.createElement('span');
    text.className = `vst-text ${fontClassFor(entry.name)} ${colorClassFor(entry.name)}`;
    if (entry.item.type === 'control' && entry.item.fontSize) text.style.fontSize = `${entry.item.fontSize}px`;
    text.textContent = displayTextFor(entry);
    preview.append(text);
  }
  return preview;
}

function fontClassFor(name) {
  if (name === 'title') return 'font-big';
  if (name === 'presetMenu') return 'font-small';
  if (name === 'refreshButton' || name === 'reloadButton') return 'font-normal';
  return 'font-very-small';
}

function colorClassFor(name) {
  if (name === 'presetMenu' || name === 'portLabel') return 'accent';
  if (['performanceTitle', 'patchTitle', 'presetCount', 'systemTitle', 'subtitle', 'dialTitle', 'ioTitle', 'brand'].includes(name)) return 'muted';
  return 'text';
}

function displayTextFor(entry) {
  return entry.name === 'presetMenu' ? 'Default Poly Saw' : (entry.item.label || entry.name);
}

function renderList() {
  const list = document.querySelector('#element-list');
  list.replaceChildren();
  for (const entry of entries()) {
    const button = document.createElement('button');
    button.textContent = entry.item.label || entry.name;
    button.title = entry.name;
    if (isSelected(entry.name)) button.classList.add('selected');
    button.addEventListener('click', event => select(entry.name, event.shiftKey));
    list.append(button);
  }
}

function render() {
  if (!state.layout) return;
  overlay.replaceChildren();
  for (const entry of entries()) {
    const rect = absoluteRect(entry.item);
    const node = document.createElement('div');
    node.className = `bounds${entry.group ? ' group' : ''}${isSelected(entry.name) ? ' selected' : ''}`;
    Object.assign(node.style, {
      left: `${rect.x}px`, top: `${rect.y}px`, width: `${rect.w}px`, height: `${rect.h}px`,
      zIndex: `${entry.item.z || 0}`
    });
    if (state.showNames || isSelected(entry.name)) {
      const label = document.createElement('span');
      label.className = 'bounds-label';
      label.textContent = entry.item.label || entry.name;
      node.append(label);
    }
    node.append(previewFor(entry));
    node.addEventListener('pointerdown', event => beginInteraction(event, entry, 'move', null, event.shiftKey));
    if (isSelected(entry.name) && state.selected.length === 1) {
      for (const direction of ['n', 'ne', 'e', 'se', 's', 'sw', 'w', 'nw']) {
        const handle = document.createElement('span');
        handle.className = `resize-handle handle-${direction}`;
        handle.title = 'Drag to resize';
        handle.addEventListener('pointerdown', event => beginInteraction(event, entry, 'resize', direction));
        node.append(handle);
      }
    }
    overlay.append(node);
  }
  renderList();
  updateInspector();
  updateActions();
}

function beginInteraction(event, entry, mode, direction = null, additive = false) {
  event.preventDefault();
  event.stopPropagation();
  if (additive) select(entry.name, true);
  else if (!isSelected(entry.name) || state.selected.length !== 1) select(entry.name);
  snapshot();
  const moving = entries().filter(item => isSelected(item.name));
  state.interaction = { mode, direction, entry, start: { x: event.clientX, y: event.clientY }, rect: { x: entry.item.x, y: entry.item.y, w: entry.item.w, h: entry.item.h }, moving };
  event.currentTarget.setPointerCapture?.(event.pointerId);
}

function snap(value) {
  return state.snap ? Math.round(value / state.snap) * state.snap : Math.round(value);
}

window.addEventListener('pointermove', event => {
  const active = state.interaction;
  if (!active) return;
  const scale = state.zoom;
  const dx = (event.clientX - active.start.x) / scale;
  const dy = (event.clientY - active.start.y) / scale;
  const item = active.entry.item;
  if (active.mode === 'move') {
    const offsetX = snap(active.rect.x + dx) - active.rect.x;
    const offsetY = snap(active.rect.y + dy) - active.rect.y;
    active.moving.forEach(selected => {
      selected.item.x = snap(selected.item.x + offsetX);
      selected.item.y = snap(selected.item.y + offsetY);
    });
  } else {
    resizeFromHandle(item, active.rect, active.direction, dx, dy);
  }
  render();
});
window.addEventListener('pointerup', () => { state.interaction = null; });

function resizeFromHandle(item, rect, direction, dx, dy) {
  let left = rect.x;
  let top = rect.y;
  let width = rect.w;
  let height = rect.h;
  if (direction.includes('e')) width = Math.max(4, snap(rect.w + dx));
  if (direction.includes('s')) height = Math.max(4, snap(rect.h + dy));
  if (direction.includes('w')) {
    width = Math.max(4, snap(rect.w - dx));
    left = snap(rect.x + rect.w - width);
  }
  if (direction.includes('n')) {
    height = Math.max(4, snap(rect.h - dy));
    top = snap(rect.y + rect.h - height);
  }
  item.x = left;
  item.y = top;
  item.w = width;
  item.h = height;
}

for (const [key, input] of Object.entries(fields)) {
  input.addEventListener('change', () => {
    const entry = selectedEntry();
    if (!entry) return;
    snapshot();
    entry.item[key] = Math.max(key === 'w' || key === 'h' ? 4 : -999, Number(input.value) || 0);
    render();
  });
}

fontSizeField.addEventListener('change', () => {
  const entry = selectedEntry();
  if (!entry || entry.item.type !== 'control') return;
  snapshot();
  entry.item.fontSize = Math.max(6, Math.min(24, Number(fontSizeField.value) || 12));
  render();
});

window.addEventListener('keydown', event => {
  const selected = entries().filter(entry => isSelected(entry.name));
  if (!selected.length || event.target.matches('input, select')) return;
  const directions = { ArrowLeft: ['x', -1], ArrowRight: ['x', 1], ArrowUp: ['y', -1], ArrowDown: ['y', 1] };
  const direction = directions[event.key];
  if (!direction) return;
  event.preventDefault();
  snapshot();
  const distance = direction[1] * (event.shiftKey ? 10 : 1);
  selected.forEach(entry => { entry.item[direction[0]] += distance; });
  render();
});

document.querySelector('#snap').addEventListener('change', event => { state.snap = event.target.checked ? 2 : 0; });
document.querySelector('#show-names').addEventListener('change', event => { state.showNames = event.target.checked; render(); });
document.querySelector('#show-groups').addEventListener('change', event => { state.showGroups = event.target.checked; render(); });
document.querySelector('#zoom').addEventListener('change', event => {
  state.zoom = Number(event.target.value);
  canvas.style.transform = `scale(${state.zoom})`;
  canvasShell.style.width = `${540 * state.zoom}px`;
  canvasShell.style.height = `${430 * state.zoom}px`;
});
document.querySelector('#undo').addEventListener('click', () => {
  if (!state.history.length) return;
  state.future.push(clone(state.layout));
  state.layout = state.history.pop();
  render();
});
document.querySelector('#redo').addEventListener('click', () => {
  if (!state.future.length) return;
  state.history.push(clone(state.layout));
  state.layout = state.future.pop();
  render();
});
function adjustLayer(delta) {
  const selected = entries().filter(entry => isSelected(entry.name));
  if (!selected.length) return;
  snapshot();
  selected.forEach(entry => { entry.item.z = (entry.item.z || 0) + delta; });
  render();
  setStatus(`${selected.length} object${selected.length === 1 ? '' : 's'} moved ${delta < 0 ? 'back' : 'forward'}.`);
}
document.querySelector('#send-back').addEventListener('click', () => adjustLayer(-1));
document.querySelector('#bring-front').addEventListener('click', () => adjustLayer(1));
document.querySelector('#reload').addEventListener('click', load);
document.querySelector('#save').addEventListener('click', async () => {
  try {
    const response = await fetch('/api/layout', { method: 'PUT', headers: { 'content-type': 'application/json' }, body: JSON.stringify(state.layout) });
    if (!response.ok) throw new Error(await response.text());
    state.history = [];
    state.future = [];
    render();
    setStatus('Saved. Close and reopen the VST editor to load this layout.');
  } catch (error) { setStatus(`Save failed: ${error.message}`, true); }
});

async function load() {
  try {
    const response = await fetch('/api/layout');
    if (!response.ok) throw new Error('Layout file was not found');
    state.layout = await response.json();
    state.history = [];
    state.future = [];
    skin.src = `/asset/${state.layout.canvas.skin}?v=${Date.now()}`;
    document.querySelector('#zoom').dispatchEvent(new Event('change'));
    render();
    setStatus('Layout loaded.');
  } catch (error) { setStatus(`Load failed: ${error.message}`, true); }
}

load();
