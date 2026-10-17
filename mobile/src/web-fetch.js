// Runs before the unchanged website script. Only local API requests cross BLE.
(() => {
  let nextId = 0;
  const pending = new Map();
  const paths = {'/api/status':'GET','/api/workouts':'GET','/api/session':'POST','/api/test/command':'POST'};
  window.__nordicReply = message => {
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    entry.cleanup();
    if (message.error) entry.reject(new Error(message.error));
    else entry.resolve({ok:message.status>=200&&message.status<300,status:message.status,text:async()=>JSON.stringify(message.body)});
  };
  window.fetch = (path, options = {}) => new Promise((resolve,reject) => {
    if (!Object.prototype.hasOwnProperty.call(paths,path) || (options.method||'GET')!==paths[path]) {
      reject(new Error('Unsupported app request')); return;
    }
    if (options.signal?.aborted) { reject(new Error('Request cancelled')); return; }
    const id = ++nextId;
    const abort = () => {
      if (!pending.delete(id)) return;
      options.signal?.removeEventListener('abort',abort);
      window.ReactNativeWebView.postMessage(JSON.stringify({type:'cancel',id}));
      reject(new Error('Request cancelled; reconnect to check status'));
    };
    pending.set(id,{resolve,reject,cleanup:()=>options.signal?.removeEventListener('abort',abort)});
    options.signal?.addEventListener('abort',abort,{once:true});
    window.ReactNativeWebView.postMessage(JSON.stringify({type:'request',id,path,method:options.method||'GET',body:options.body}));
  });
})();
