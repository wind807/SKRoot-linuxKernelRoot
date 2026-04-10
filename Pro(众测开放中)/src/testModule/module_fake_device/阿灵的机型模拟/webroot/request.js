const RequestApi = (() => {
  function postText(path, bodyText = "") {
    const url = new URL(path, window.location.href);
    return fetch(url, { method: 'POST', body: bodyText });
  }

  async function getCurrentSh() {
    const resp = await postText('/getCurrentSh');
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  async function setCurrentSh(sh) {
    const resp = await postText('/setCurrentSh', sh);
    return resp.ok ? await resp.text() : ('HTTP ' + resp.status);
  }

  return {
    getCurrentSh,
    setCurrentSh
  };
})();
