(() => {
  const out = document.getElementById("out");
  const cmd = document.getElementById("cmd");
  const sendBtn = document.getElementById("sendBtn");
  const connDot = document.getElementById("connDot");
  const connText = document.getElementById("connText");
  const quickActions = document.getElementById("quickActions");
  const fileBtn = document.getElementById("fileBtn");
  const autoBtn = document.getElementById("autoBtn");
  const sheetOverlay = document.getElementById("sheetOverlay");
  const inlineTip = document.getElementById("inlineTip");

  const terminalCore = window.SKTerminalCore?.createTerminalCore({
    out,
    connDot,
    connText,
  });

  if (!terminalCore) {
    console.error("SKTerminalCore not loaded");
    return;
  }

  async function refreshHistory() {
    let history = await RequestApi.getQuickActions();
    if (!history || !Array.isArray(history) || history.length === 0) history = [];
    quickActions.innerHTML = history.reverse().map(cmdStr => {
      const displayStr = cmdStr.length > 17 ? cmdStr.substring(0, 17) + '...' : cmdStr;
      return `<div class="action-chip" data-cmd="${cmdStr}" title="${cmdStr}">${displayStr}</div>`;
    }).join('');
  }

  function updateInlineTip() {
    if (!inlineTip) return;
    const typing = !!(cmd.value || "").trim();
    const text = (out.textContent || "")
    .replace(/\u00a0/g, " ")
    .trim();
    const onlyPrompt = text === "#" || text === "";
    inlineTip.classList.toggle("hidden", typing || !onlyPrompt);
  }

  async function sendCommand() {
    const v = (cmd.value || "").trim();
    if (!v) return;
    terminalCore.appendLine("# " + v, true);
    cmd.value = "";
    cmd.focus();
    updateInlineTip();
    sendBtn.disabled = true;
    try {
      await RequestApi.sendCommand(v);
      setTimeout(refreshHistory, 300);
    } catch (e) {
      terminalCore.appendLine("ERR: 发送失败 - " + (e && e.message ? e.message : String(e)));
    } finally {
      sendBtn.disabled = false;
      terminalCore.scrollBottom();
    }
  }

  const app = {
    elements: {
      out,
      cmd,
      sendBtn,
      connDot,
      connText,
      quickActions,
      fileBtn,
      autoBtn,
      sheetOverlay,
      inlineTip,
    },
    refreshHistory,
    sendCommand,
    appendLine: terminalCore.appendLine,
    setConn: terminalCore.setConn,
    scrollBottom: terminalCore.scrollBottom,
    highlightText: terminalCore.highlightText,
    consumeChunk: terminalCore.consumeChunk,
    closeAllSheets: null,
    openAutoSheet: null,
  };

  window.SKRootApp = app;

  quickActions.addEventListener("click", (e) => {
    const chip = e.target.closest(".action-chip");
    if (chip && chip.dataset.cmd) {
      cmd.value = chip.dataset.cmd;
      cmd.focus();
      updateInlineTip();
    }
  });

  cmd.addEventListener("keydown", (ev) => {
    if (ev.key === "Enter") {
      ev.preventDefault();
      sendCommand();
    }
  });

  sendBtn.addEventListener("click", sendCommand);
  cmd.addEventListener("input", updateInlineTip);

  terminalCore.appendLine("#", true);
  terminalCore.setConn(null, "连接中");
  refreshHistory();
  updateInlineTip();
  cmd.focus();
  setInterval(terminalCore.tick, 1000);
})();
