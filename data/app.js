function api(path, params){
  let url = path;
  if (params) {
    const qs = Object.keys(params).map(k => encodeURIComponent(k)+'='+encodeURIComponent(params[k])).join('&');
    url += (url.indexOf('?')===-1? '?':'&') + qs;
  }
  return fetch(url).then(r => r.text()).catch(e => "ERR: " + e);
}

const pad = document.getElementById('pad');
const dot = document.getElementById('dot');
const status = document.getElementById('status');
const spinVal = document.getElementById('spinVal');
let dragging = false;
let ch = () => document.getElementById('ch').value;

function sendSpin(val){
  spinVal.textContent = val;
  api('/api/spin', {ch: ch(), speed: Math.round(val)}).then(t => status.textContent = t);
}

function stop(){ api('/api/stop', {ch: ch()}).then(t => status.textContent = t); spinVal.textContent = 0; }

function center(){ dot.style.left = '50%'; dot.style.top = '50%'; stop(); }

function posToVal(x, w){
  // map x from [0..w] to [-100..100]
  const v = ((x / w) * 200) - 100;
  return Math.max(-100, Math.min(100, v));
}

function updateFromEvent(e){
  const rect = pad.getBoundingClientRect();
  const cx = (e.touches ? e.touches[0].clientX : e.clientX) - rect.left;
  const cy = (e.touches ? e.touches[0].clientY : e.clientY) - rect.top;
  const x = Math.max(0, Math.min(rect.width, cx));
  const y = Math.max(0, Math.min(rect.height, cy));
  dot.style.left = (x / rect.width * 100) + '%';
  dot.style.top = (y / rect.height * 100) + '%';
  // for now, map X to spin speed full range
  const sp = posToVal(x, rect.width);
  sendSpin(sp);
}

pad.addEventListener('mousedown', e => { dragging = true; updateFromEvent(e); });
pad.addEventListener('touchstart', e => { dragging = true; updateFromEvent(e); e.preventDefault(); });
window.addEventListener('mousemove', e => { if (dragging) updateFromEvent(e); });
window.addEventListener('touchmove', e => { if (dragging) updateFromEvent(e); });
window.addEventListener('mouseup', e => { if (dragging) { dragging = false; } });
window.addEventListener('touchend', e => { if (dragging) { dragging = false; } });

document.getElementById('stopBtn').addEventListener('click', stop);
document.getElementById('centerBtn').addEventListener('click', center);
document.getElementById('leftBtn').addEventListener('click', () => { sendSpin(-100); });
document.getElementById('rightBtn').addEventListener('click', () => { sendSpin(100); });

// init
center();