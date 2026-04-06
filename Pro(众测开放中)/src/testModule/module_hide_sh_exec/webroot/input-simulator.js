(() => {
  const app = window.SKRootApp;
  const ui = window.SKUiUtils;
  if (!app || !ui) return;

  const { cmd, autoBtn, fileBtn, sheetOverlay } = app.elements;

  const autoSheet = document.getElementById("autoSheet");
  const autoList = document.getElementById("autoList");
  const autoClose = document.getElementById("autoClose");
  const autoExecBtn = document.getElementById("autoExecBtn");

  let tasks = [];
  let saveTimer = null;
  let isEditing = false;

  function normalizeTasks(list) {
    return Array.isArray(list) ? list.map(item => ({
      cmd: item?.cmd || "",
      delay: item?.delay ?? "",
    })) : [];
  }

  function renderTasks() {
      if (tasks.length === 0) tasks = [{ cmd: "", delay: "" }];
      
      autoList.innerHTML = tasks.map((t, i) => {
        const delayHtml = i === 0 ? '' : `
          <div class="auto-ctrl" style="margin-bottom: 8px;">
            等待 <input type="number" class="auto-delay auto-input" value="${ui.safeHtml(t.delay || '')}" placeholder="0"> 秒后执行 ⬇️
          </div>
        `;
        
        return `
        <div class="auto-row" data-index="${i}">
          <div class="auto-num">${i + 1}</div>
          <div class="auto-content">
            ${delayHtml}
            <input type="text" class="auto-cmd auto-input" value="${ui.safeHtml(t.cmd || '')}" placeholder="输入指令">
          </div>
          <div class="auto-del">×</div>
        </div>
      `}).join('');
    }

  function triggerSave() {
    if (saveTimer) clearTimeout(saveTimer);
    saveTimer = setTimeout(async () => {
      await RequestApi.saveAutoTasks(JSON.stringify(tasks));
      isEditing = false;
    }, 500);
  }

  async function openAutoSheet() {
    autoBtn.classList.add("active");
    fileBtn.classList.remove("active");
    autoSheet.classList.add("show");
    sheetOverlay.classList.add("show");

    renderTasks();

    try {
      const data = await RequestApi.getAutoTasks();
      if (data && !isEditing) {
        tasks = normalizeTasks(data);
        renderTasks();
      }
    } catch (e) {
      console.warn("Sync failed");
    }
  }

  function closeAutoSheet() {
    autoSheet.classList.remove("show");
    autoBtn.classList.remove("active");
    if (!document.getElementById("fileSheet").classList.contains("show")) {
      sheetOverlay.classList.remove("show");
    }
  }

  function replaceTasks(newTasks) {
    tasks = normalizeTasks(newTasks);
    renderTasks();
  }

  function getTasks() {
    return tasks;
  }

  autoBtn.onclick = openAutoSheet;
  autoClose.onclick = closeAutoSheet;

  autoSheet.onclick = (e) => {
    if (e.target.id === "addTaskBtn") {
      tasks.push({ cmd: "", delay: "" });
      renderTasks();
      triggerSave();
      return;
    }
    if (e.target.classList.contains("auto-del")) {
      const idx = Number(e.target.closest(".auto-row").dataset.index);
      tasks.splice(idx, 1);
      renderTasks();
      triggerSave();
    }
  };

  autoList.oninput = (e) => {
    isEditing = true;
    const row = e.target.closest(".auto-row");
    if (!row) return;
    const idx = Number(row.dataset.index);
    if (e.target.classList.contains("auto-cmd")) tasks[idx].cmd = e.target.value;
    if (e.target.classList.contains("auto-delay")) tasks[idx].delay = e.target.value;
    triggerSave();
  };

  autoExecBtn.onclick = async () => {
    closeAutoSheet();
    for (let i = 0; i < tasks.length; i++) {
      const t = tasks[i];
      if (!t.cmd || !t.cmd.trim()) continue;
      const waitTime = parseFloat(t.delay) || 0;
      if (i > 0 && waitTime > 0) {
        await new Promise(res => setTimeout(res, waitTime * 1000));
      }
      cmd.value = t.cmd;
      await app.sendCommand();
    }
  };

  sheetOverlay.addEventListener("click", () => {
    if (typeof app.closeAllSheets === 'function') app.closeAllSheets();
  });

  window.SKInputSimulator = {
    open: openAutoSheet,
    close: closeAutoSheet,
    getTasks,
    replaceTasks,
    renderTasks,
  };

  app.openAutoSheet = openAutoSheet;

  const prevCloseAllSheets = app.closeAllSheets;
  app.closeAllSheets = () => {
    if (typeof prevCloseAllSheets === 'function') prevCloseAllSheets();
    closeAutoSheet();
  };
})();
