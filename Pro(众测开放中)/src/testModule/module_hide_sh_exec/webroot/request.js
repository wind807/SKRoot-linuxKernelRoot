const RequestApi = (() => {
  function postText(path, bodyText = "") {
    const url = new URL(path, window.location.href);
    return fetch(url, { method: 'POST', body: bodyText });
  }

  function decodeCppUrl(str) {
    if (typeof str !== 'string') return str;
    return decodeURIComponent(str.replace(/\+/g, ' '));
  }

  function encodeCppUrl(str) {
    if (typeof str !== 'string') return str;
    return encodeURIComponent(str).replace(/%20/g, '+');
  }

  async function sendCommand(cmd) {
    const resp = await postText('/sendCommand', cmd);
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  async function getNewOutput() {
    const resp = await postText('/getNewOutput');
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  async function getQuickActions() {
    const resp = await postText('/getQuickActions');
    if (!resp.ok) return null;
    try {
      const arr = JSON.parse(await resp.text());
      if (!Array.isArray(arr)) return null;
      return arr.map(item =>
        typeof item === 'string' ? decodeCppUrl(item) : item
      );
    } catch (e) {
      console.error('getQuickActions parse/decode failed:', e);
      return null;
    }
  }

  async function listDir(path) {
    const resp = await postText('/listDir', path);
    return resp.ok ? JSON.parse(await resp.text()) : null;
  }

  async function getAutoTasks() {
    const resp = await postText('/getAutoTasks');
    return resp.ok ? JSON.parse(await resp.text()) : [];
  }

  async function saveAutoTasks(tasks) {
    const resp = await postText('/saveAutoTasks', tasks);
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  async function checkFileType(filePath) {
    const resp = await postText('/checkFileType', filePath);
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  async function checkExecMount(dir) {
    const resp = await postText('/checkExecMount', dir);
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  return {
    sendCommand,
    getNewOutput,
    getQuickActions,
    listDir,
    getAutoTasks,
    saveAutoTasks,
    checkFileType,
    checkExecMount
  };
})();