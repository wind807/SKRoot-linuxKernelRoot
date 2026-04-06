(() => {
  function safeHtml(text) {
    return String(text ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function joinPath(base, name) {
    const left = String(base || "").replace(/\/+$/, "");
    const right = String(name || "").replace(/^\/+/, "");
    if (!left) return `/${right}`;
    if (!right) return left || "/";
    return `${left}/${right}`;
  }

  function dirname(path) {
    const normalized = String(path || "/").replace(/\/+$/, "") || "/";
    if (normalized === "/") return "/";
    const parts = normalized.split('/').filter(Boolean);
    parts.pop();
    return '/' + parts.join('/');
  }

  function formatBytes(val) {
    if (val === undefined || val === null || val === '') return '';
    
    // 尝试转为数字，如果原本传过来的已经是 "1.2KB" 这种字符串，isNaN 会返回 true，就直接输出原字符串
    const bytes = Number(val);
    if (isNaN(bytes)) return val; 
    
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    
    // 保留最多两位小数
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  }

  function showToast(message, duration = 1000) {
    const tip = document.createElement("div");
    tip.style = "position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);background:rgba(0,0,0,0.7);color:#fff;padding:8px 16px;border-radius:20px;font-size:12px;z-index:999";
    tip.textContent = message;
    document.body.appendChild(tip);
    setTimeout(() => tip.remove(), duration);
  }

  function showModal(el) {
    el?.classList.add("show");
  }

  function hideModal(el) {
    el?.classList.remove("show");
  }

  window.SKUiUtils = {
    safeHtml,
    joinPath,
    dirname,
    formatBytes,
    showToast,
    showModal,
    hideModal,
  };
})();
