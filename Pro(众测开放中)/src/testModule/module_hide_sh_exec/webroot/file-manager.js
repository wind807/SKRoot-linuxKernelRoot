(() => {
  const app = window.SKRootApp;
  const ui = window.SKUiUtils;
  if (!app || !ui) return;

  const { cmd, fileBtn, autoBtn, sheetOverlay } = app.elements;

  const fileSheet = document.getElementById("fileSheet");
  const fileList = document.getElementById("fileList");
  const fileBack = document.getElementById("fileBack");
  const pathInput = document.getElementById("pathInput");
  const pathGo = document.getElementById("pathGo");
  const fileRefresh = document.getElementById("fileRefresh");
  const fileClose = document.getElementById("fileClose");

  const actionModal = document.getElementById("actionModal");
  const actAuto = document.getElementById("actAuto");
  const actExec = document.getElementById("actExec");
  const actRename = document.getElementById("actRename");
  const actionCancel = document.getElementById("actionCancel");

  const execModal = document.getElementById("execModal");
  const execCode = document.getElementById("execCode");
  const argInput = document.getElementById("argInput");
  const execCancel = document.getElementById("execCancel");
  const execConfirm = document.getElementById("execConfirm");

  const autoModal = document.getElementById("autoModal");
  const autoCode = document.getElementById("autoCode");
  const autoArgInput = document.getElementById("autoArgInput");
  const autoCancel = document.getElementById("autoCancel");
  const autoConfirm = document.getElementById("autoConfirm");

  const renameModal = document.getElementById("renameModal");
  const renameOldName = document.getElementById("renameOldName");
  const renameInput = document.getElementById("renameInput");
  const renameCancel = document.getElementById("renameCancel");
  const renameConfirm = document.getElementById("renameConfirm");

  const actDelete = document.getElementById("actDelete");
  const deleteModal = document.getElementById("deleteModal");
  const deleteTargetName = document.getElementById("deleteTargetName");
  const deleteCancel = document.getElementById("deleteCancel");
  const deleteConfirm = document.getElementById("deleteConfirm");
  
  const confirmModal = document.getElementById("confirmModal");
  const confirmCancel = document.getElementById("confirmCancel");
  const confirmOk = document.getElementById("confirmOk");

  let currentPath = "/sdcard";
  let currentFile = null;

  async function loadDir(path) {
    currentPath = path || "/sdcard";
    pathInput.value = currentPath;
    pathInput.readOnly = true;
    pathGo.style.display = "none";
    fileList.innerHTML = '<div style="padding:20px;text-align:center;color:#94a3b8;font-size:12px">读取中...</div>';
    let files = await RequestApi.listDir(currentPath);
    if (!files) {
      files = [{ name: "modules", isDir: true, date: "2026-03-26", time: "22:10", size: 0}];
    }
    renderFiles(files);
  }

  function renderFiles(files) {
      fileList.innerHTML = files.map(f => {
        const icon = f.isDir ? "📁" : (f.canExec ? '<span style="color:#9333ea">⚙️</span>' : "📄");
        const fullPath = ui.joinPath(currentPath, f.name);
        const displaySize = f.isDir ? "文件夹" : ui.formatBytes(f.size);
        return `
          <div class="file-row" data-path="${ui.safeHtml(fullPath)}" data-isdir="${!!f.isDir}" data-exec="${!!f.canExec}">
            <div class="file-icon">${icon}</div>
            <div class="file-info">
              <div class="file-name">${ui.safeHtml(f.name)}</div>
              <div class="file-meta">${ui.safeHtml(f.date || '')} ${ui.safeHtml(f.time || '')} | ${ui.safeHtml(displaySize)}</div>
            </div>
          </div>
        `;
      }).join('');
      fileList.scrollTop = 0;
    }

  function openFileSheet() {
    autoBtn.classList.remove("active");
    fileBtn.classList.add("active");
    fileSheet.classList.add("show");
    sheetOverlay.classList.add("show");
    loadDir(currentPath);
  }

  function closeFileSheet() {
    fileBtn.classList.remove("active");
    fileSheet.classList.remove("show");
    pathInput.readOnly = true;
    pathGo.style.display = "none";
    if (!document.getElementById("autoSheet").classList.contains("show")) {
      sheetOverlay.classList.remove("show");
    }
  }

  async function performTransfer() {
    let fullCmd;
    try {
      fullCmd = window.SKTerminalCore.buildShCommand(currentFile, autoArgInput.value);
    } catch (e) {
      alert(e.message || "参数格式错误");
      return;
    }
    const newTasks = [{ cmd: fullCmd, delay: "0" }];

    confirmOk.disabled = true;
    confirmOk.textContent = "同步中...";

    try {
      const res = await RequestApi.saveAutoTasks(JSON.stringify(newTasks));
      if (res === "OK") {
        ui.hideModal(confirmModal);
        ui.hideModal(autoModal);
        closeFileSheet();
        window.SKInputSimulator.replaceTasks(newTasks);
        app.openAutoSheet();
      }
    } catch (e) {
      alert("同步至服务器失败，请检查网络");
    } finally {
      confirmOk.disabled = false;
      confirmOk.textContent = "确定覆盖";
    }
  }

  pathInput.onclick = () => {
    pathInput.readOnly = false;
    pathInput.focus();
    pathGo.style.display = "block";
  };

  pathGo.onclick = () => loadDir(pathInput.value.trim() || currentPath);
  fileBack.onclick = () => loadDir(ui.dirname(currentPath));
  fileRefresh.onclick = () => loadDir(currentPath);

  fileList.onclick = (e) => {
    const row = e.target.closest('.file-row');
    if (!row) return;
    const path = row.dataset.path;
    const isDir = row.dataset.isdir === "true";

    if (isDir) {
      loadDir(path);
    } else {
      currentFile = path;
      ui.showModal(actionModal);
    }
  };

  fileBtn.onclick = openFileSheet;
  fileClose.onclick = closeFileSheet;

  actionCancel.onclick = () => ui.hideModal(actionModal);
  actExec.onclick = () => {
    ui.hideModal(actionModal);
    execCode.textContent = currentFile;
    argInput.value = "";
    ui.showModal(execModal);
  };
  actAuto.onclick = () => {
    ui.hideModal(actionModal);
    autoCode.textContent = currentFile;
    autoArgInput.value = "";
    ui.showModal(autoModal);
  };
  actRename.onclick = () => {
    ui.hideModal(actionModal);
    renameOldName.textContent = currentFile;
    renameInput.value = currentFile.split('/').pop();
    ui.showModal(renameModal);
    setTimeout(() => renameInput.focus(), 100);
  };
  actDelete.onclick = () => {
    ui.hideModal(actionModal);
    deleteTargetName.textContent = currentFile;
    ui.showModal(deleteModal);
  };

  execCancel.onclick = () => ui.hideModal(execModal);
  execConfirm.onclick = () => {
    let finalCmd;
    try {
      finalCmd = window.SKTerminalCore.buildShCommand(currentFile, argInput.value);
    } catch (e) {
      alert(e.message || "参数格式错误");
      return;
    }
    ui.hideModal(execModal);
    closeFileSheet();
    cmd.value = finalCmd;
    app.sendCommand();
  };

  autoCancel.onclick = () => ui.hideModal(autoModal);
  autoConfirm.onclick = () => {
    const tasks = window.SKInputSimulator?.getTasks?.() || [];
    const hasContent = tasks.length > 0 && tasks[0].cmd && tasks[0].cmd.trim() !== "";
    if (hasContent) ui.showModal(confirmModal);
    else performTransfer();
  };

  confirmCancel.onclick = () => ui.hideModal(confirmModal);
  confirmOk.onclick = () => performTransfer();

  renameCancel.onclick = () => ui.hideModal(renameModal);
  renameConfirm.onclick = () => {
    const newName = renameInput.value.trim();
    if (!newName) return;

    const dir = ui.dirname(currentFile);
    const newPath = ui.joinPath(dir, newName);
    const finalCmd = `mv ${window.SKTerminalCore.shellQuote(currentFile)} ${window.SKTerminalCore.shellQuote(newPath)}`;

    ui.hideModal(renameModal);
    closeFileSheet();
    cmd.value = finalCmd;
    app.sendCommand();
  };

  deleteCancel.onclick = () => ui.hideModal(deleteModal);
  deleteConfirm.onclick = () => {
    const finalCmd = `rm -rf ${window.SKTerminalCore.shellQuote(currentFile)}`;
    
    ui.hideModal(deleteModal);
    closeFileSheet();
    
    // 填入输入框并直接发射
    cmd.value = finalCmd;
    app.sendCommand();
  };

  window.SKFileManager = {
    open: openFileSheet,
    close: closeFileSheet,
    loadDir,
  };

  const prevCloseAllSheets = app.closeAllSheets;
  app.closeAllSheets = () => {
    if (typeof prevCloseAllSheets === 'function') prevCloseAllSheets();
    closeFileSheet();
  };
})();
