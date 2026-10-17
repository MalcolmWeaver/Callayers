function(generation) {
  'use strict';
  // Only bounded state crosses to the trusted host; the card gets no native API.
  const send = data => parent.postMessage(Object.assign({channel:'callayers-card',generation}, data), '*');
  window.addEventListener('message', event => {
    const data = event.data;
    if (event.source !== parent || !data || data.channel !== 'callayers-host' || data.generation !== generation) return;
    try {
      if (data.operation === 'restore') {
        const state = JSON.parse(data.state);
        if (window.CallayersCard && typeof window.CallayersCard.restore === 'function') window.CallayersCard.restore(state);
        send({operation:'restored',request:data.request});
      } else if (data.operation === 'snapshot') {
        const value = window.CallayersCard && typeof window.CallayersCard.snapshot === 'function' ? window.CallayersCard.snapshot() : {};
        const state = JSON.stringify(value);
        if (typeof state !== 'string' || state.length > 16384) throw new Error('State too large');
        send({operation:'snapshot',request:data.request,state});
      }
    } catch (_) { send({operation:'error',request:data.request}); }
  });
  window.addEventListener('DOMContentLoaded', () => send({operation:'ready',request:0}), {once:true});
}
