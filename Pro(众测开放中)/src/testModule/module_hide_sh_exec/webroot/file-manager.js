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
  
  const confirmQuickTaskModal = document.getElementById("confirmQuickTaskModal");
  const confirmQuickTaskCancel = document.getElementById("confirmQuickTaskCancel");
  const confirmQuickTaskOk = document.getElementById("confirmQuickTaskOk");

  const execMountModal = document.getElementById("execMountModal");
  const execMountTarget = document.getElementById("execMountTarget");
  const execMountCancel = document.getElementById("execMountCancel");
  const execMountConfirm = document.getElementById("execMountConfirm");

  let currentPath = "/sdcard";
  let currentFile = null;
  let pendingMode = null;
  let pendingPreparedFile = null;
  let hideDirCache = "";

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

  function getFileBaseName(filePath) {
    return String(filePath || "").split('/').pop() || "";
  }

  async function getHideDir() {
    if (hideDirCache) return hideDirCache;
    const dir = String(await RequestApi.getHideDir() || "").trim();
    if (!dir || /^HTTP\s+\d+/i.test(dir)) {
      throw new Error("获取隐藏目录失败，请稍后重试");
    }
    hideDirCache = dir;
    return hideDirCache;
  }

  async function getCopiedTargetPath(filePath) {
    const hideDir = await getHideDir();
    return ui.joinPath(hideDir, getFileBaseName(filePath));
  }

  function clearPendingPreparedFile() {
    pendingPreparedFile = null;
  }

  async function resolvePreparedFile(filePath) {
    const fileType = await RequestApi.checkFileType(filePath);
    if (fileType === "shell_script") return { sourceFile: filePath, resolvedFile: filePath, fileType, needsCopy: false, };
    if (fileType === "executable_arm64" || fileType === "executable_arm32") {
      const dir = window.SKTerminalCore.getPathDir(filePath);
      const mountState = await RequestApi.checkExecMount(dir);
      if (mountState === "can exec") return { sourceFile: filePath, resolvedFile: filePath, fileType, needsCopy: false, };
      if (mountState === "can not exec") return { sourceFile: filePath, resolvedFile: await getCopiedTargetPath(filePath), fileType, needsCopy: true, };
      throw new Error("执行目录检测结果异常：" + (mountState || "未知"));
    }
    throw new Error("当前文件不支持直接执行：" + (fileType || "未知类型"));
  }

  function showPreparedModal() {
    if (!pendingPreparedFile) return;
    if (pendingMode === "exec") {
      execCode.textContent = pendingPreparedFile.resolvedFile;
      argInput.value = "";
      ui.showModal(execModal);
      return;
    }
    if (pendingMode === "simulator") {
      autoCode.textContent = pendingPreparedFile.resolvedFile;
      autoArgInput.value = "";
      ui.showModal(autoModal);
    }
  }

  async function prepareAndOpenFlow(mode) {
    pendingMode = mode;
    clearPendingPreparedFile();
    try {
      const prepared = await resolvePreparedFile(currentFile);
      pendingPreparedFile = prepared;
      ui.hideModal(actionModal);
      if (prepared.needsCopy) {
        execMountTarget.innerHTML = `<div class="modal-path-from">当前文件：${ui.safeHtml(prepared.sourceFile)}</div>
<div class="modal-path-to">将复制到：${ui.safeHtml(prepared.resolvedFile)}</div>`;
        ui.showModal(execMountModal);
        return;
      }
      showPreparedModal();
    } catch (e) {
      alert(e.message || "文件检查失败，请稍后重试");
    }
  }

  function buildPendingCommand(rawArgs) {
    if (!pendingPreparedFile) throw new Error("当前没有可执行文件");
    if (pendingPreparedFile.fileType === "shell_script") return window.SKTerminalCore.buildShCommand(pendingPreparedFile.resolvedFile, rawArgs);
    if (pendingPreparedFile.fileType === "executable_arm64" || pendingPreparedFile.fileType === "executable_arm32") return window.SKTerminalCore.buildExecCommand(pendingPreparedFile.resolvedFile, rawArgs);
    throw new Error("当前文件类型不支持执行");
  }

  async function executeImmediateCommand(finalCmd) {
    closeFileSheet();
    cmd.value = finalCmd;
    await app.sendCommand();
  }

  async function submitPreparedCommand(options = {}) {
    const skipConfirm = !!options.skipConfirm;
    if (!pendingPreparedFile) {
      alert("当前没有可执行文件");
      return;
    }

    if (pendingMode === "exec") {
      let finalCmd;
      try {
        finalCmd = buildPendingCommand(argInput.value);
      } catch (e) {
        alert(e.message || "参数格式错误");
        return;
      }
      ui.hideModal(execModal);
      await executeImmediateCommand(finalCmd);
      return;
    }

    if (pendingMode === "simulator") {
      let fullCmd;
      try {
        fullCmd = buildPendingCommand(autoArgInput.value);
      } catch (e) {
        alert(e.message || "参数格式错误");
        return;
      }
      const tasks = window.SKInputSimulator?.getTasks?.() || [];
      const hasContent = tasks.length > 0 && tasks[0].cmd && tasks[0].cmd.trim() !== "";
      if (hasContent && !skipConfirm) {
        ui.showModal(confirmQuickTaskModal);
        return;
      }
      const newTasks = [{ cmd: fullCmd, delay: "0" }];
      confirmQuickTaskOk.disabled = true;
      confirmQuickTaskOk.textContent = "同步中...";
      try {
        const res = await RequestApi.saveAutoTasks(JSON.stringify(newTasks));
        if (res === "OK") {
          ui.hideModal(confirmQuickTaskModal);
          ui.hideModal(autoModal);
          closeFileSheet();
          window.SKInputSimulator.replaceTasks(newTasks);
          app.openAutoSheet();
        }
      } catch (e) {
        alert("同步至服务器失败，请检查网络");
      } finally {
        confirmQuickTaskOk.disabled = false;
        confirmQuickTaskOk.textContent = "确定覆盖";
      }
    }
  }

  async function copyPreparedFileAndContinue() {
    if (!pendingPreparedFile || !pendingPreparedFile.needsCopy) return;
    const srcFile = pendingPreparedFile.sourceFile;
    const dstFile = pendingPreparedFile.resolvedFile;
    const copyCmd = `cp -f ${window.SKTerminalCore.shellQuote(srcFile)} ${window.SKTerminalCore.shellQuote(dstFile)}`;
    execMountConfirm.disabled = true;
    execMountConfirm.textContent = "拷贝中...";
    try {
      await executeImmediateCommand(copyCmd);
      const adbMountState = await RequestApi.checkExecMount(window.SKTerminalCore.getPathDir(dstFile));
      if (pendingPreparedFile.fileType !== "shell_script" && adbMountState !== "can exec") {
        throw new Error("拷贝完成，但隐藏目录仍然不可执行，请检查系统环境");
      }
      pendingPreparedFile = {
        ...pendingPreparedFile,
        needsCopy: false,
      };
      ui.hideModal(execMountModal);
      showPreparedModal();
    } catch (e) {
      alert(e.message || "拷贝失败，请稍后重试");
    } finally {
      execMountConfirm.disabled = false;
      execMountConfirm.textContent = "一键拷贝并继续";
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
  actExec.onclick = () => prepareAndOpenFlow("exec");
  actAuto.onclick = () => prepareAndOpenFlow("simulator");
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
  execConfirm.onclick = async () => {
    await submitPreparedCommand();
  };

  autoCancel.onclick = () => ui.hideModal(autoModal);
  autoConfirm.onclick = async () => {
    await submitPreparedCommand();
  };

  confirmQuickTaskCancel.onclick = () => ui.hideModal(confirmQuickTaskModal);
  confirmQuickTaskOk.onclick = () => submitPreparedCommand({ skipConfirm: true });

  execMountCancel.onclick = () => {
    ui.hideModal(execMountModal);
    clearPendingPreparedFile();
  };
  execMountConfirm.onclick = () => copyPreparedFileAndContinue();

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
