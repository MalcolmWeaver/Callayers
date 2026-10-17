// The trusted parent owns state validation. Cards remain opaque-origin sandboxed
// frames on both SDK versions; no same-origin privilege or native object is added.
'use strict';
window.CallayersHost = (() => {
  let bridge = '', frame = null, generation = 0, sequence = 0, pending = null;
  let status = 'empty', initialState = '{}', scale = 1;
  const policies = `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; frame-src 'none'; media-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; worker-src 'none'">`;
  function validValue(value, depth = 0) {
    if (depth > 4) return false;
    if (value === null || typeof value === 'boolean') return true;
    if (typeof value === 'number') return Number.isFinite(value);
    if (typeof value === 'string') return value.length <= 4096;
    if (Array.isArray(value)) return value.length <= 128 && value.every(v => validValue(v, depth + 1));
    if (typeof value !== 'object') return false;
    const keys = Object.keys(value);
    return keys.length <= 64 && keys.every(key => key.length <= 80 &&
      !['__proto__','constructor','prototype'].includes(key) && validValue(value[key], depth + 1));
  }
  function validState(text) {
    if (typeof text !== 'string' || text.length > 16384 || new TextEncoder().encode(text).length > 16384) return false;
    try {
      const state = JSON.parse(text);
      return state !== null && typeof state === 'object' && !Array.isArray(state) && validValue(state);
    } catch (_) { return false; }
  }
  function send(operation, request, state) {
    frame.contentWindow.postMessage({channel:'callayers-host', generation, operation, request, state}, '*');
  }
  window.addEventListener('message', event => {
    if (!frame || event.source !== frame.contentWindow) return;
    const data = event.data;
    if (!data || typeof data !== 'object' || data.channel !== 'callayers-card' || data.generation !== generation) return;
    if (data.operation === 'ready' && status === 'loading') send('restore', 0, initialState);
    else if (data.operation === 'restored' && status === 'loading') status = 'ready';
    else if (data.operation === 'error') {
      if (status === 'loading' && data.request === 0) status = 'error:Could not restore the card values';
      else if (pending && data.request === pending.request) pending.result = 'error:Could not save the card values';
    } else if (data.operation === 'snapshot' && pending && data.request === pending.request) {
      pending.result = validState(data.state) ? data.state : 'error:The card returned invalid saved values';
    }
  });
  return Object.freeze({
    configure(source) { bridge = source; },
    clear() {
      ++generation; frame = null; pending = null; status = 'empty';
      document.getElementById('scene').replaceChildren();
    },
    load(html, state, width = 640, height = 400) {
      if (typeof html !== 'string' || !validState(state)) return 'Invalid card or saved values';
      if (!Number.isFinite(width) || !Number.isFinite(height) || width < 1 || width > 1280 || height < 1 || height > 720) return 'Invalid card dimensions';
      this.clear();
      initialState = state; status = 'loading';
      frame = document.createElement('iframe');
      frame.setAttribute('sandbox', 'allow-scripts');
      frame.setAttribute('allow', "camera 'none'; microphone 'none'; geolocation 'none'; clipboard-read 'none'; clipboard-write 'none'; fullscreen 'none'; autoplay 'none'");
      frame.referrerPolicy = 'no-referrer';
      frame.style.width = `${width}px`; frame.style.height = `${height}px`;
      frame.style.transform = `scale(${scale})`;
      const script = `(${bridge})(${generation});`.replace(/<\/script/gi, '<\\/script');
      frame.srcdoc = '<!doctype html><html><head><meta charset="utf-8">' + policies +
        '<style>html,body{margin:0;width:100%;height:100%;background:transparent;overflow:hidden}</style>' +
        '<script>' + script + '</scr' + 'ipt></head><body>' + html + '</body></html>';
      document.getElementById('scene').append(frame);
      return 'ok';
    },
    status() { return status; },
    scale(value) { scale = value; if (frame) frame.style.transform = `scale(${scale})`; },
    snapshot() {
      if (status !== 'ready') throw new Error('The card is not ready');
      const request = ++sequence;
      pending = {request, result:'', started:performance.now()};
      send('snapshot', request);
      return request;
    },
    result(request) {
      if (!pending || request !== pending.request) return 'error:That save request is no longer current';
      if (!pending.result && performance.now() - pending.started > 3000) return 'error:The card did not return its saved values';
      return pending.result;
    }
  });
})();
