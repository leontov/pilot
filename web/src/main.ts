const tabs = [
  { id: "dialog", label: "Диалог" },
  { id: "memory", label: "Память" },
  { id: "programs", label: "Программы" },
  { id: "synth", label: "Синтез" },
  { id: "chain", label: "Блокчейн" },
  { id: "status", label: "Статус" },
  { id: "cluster", label: "Кластер" }
];

function createElement(tag: string, className?: string, text?: string): HTMLElement {
  const el = document.createElement(tag);
  if (className) el.className = className;
  if (text) el.textContent = text;
  return el;
}

function digitsFromExpression(expr: string): number[] {
  const result: number[] = [];
  for (const ch of expr.replace(/\s+/g, "")) {
    if (ch >= "0" && ch <= "9") {
      result.push(Number(ch));
    } else if (ch === "+") {
      result.push(43);
    } else if (ch === "-") {
      result.push(45);
    } else if (ch === "*") {
      result.push(42);
    } else if (ch === "/") {
      result.push(47);
    }
  }
  return result;
}

function renderDialog(container: HTMLElement) {
  const historyEntries: { input: string; answer: unknown; timestamp: string }[] = [];

  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.type = "text";
  input.placeholder = "Введите выражение (например, 2+2)";
  const submit = document.createElement("button");
  submit.type = "submit";
  submit.textContent = "Выполнить";
  const output = createElement("pre", "output");

  const historyContainer = createElement("div", "history-container");
  const historyTitle = createElement("h3", "history-title", "История");
  const historyList = createElement("ul", "history-list");
  const historyEmpty = createElement("p", "history-empty", "Диалогов пока нет.");

  const renderHistory = () => {
    historyList.innerHTML = "";
    if (historyEntries.length === 0) {
      if (!historyContainer.contains(historyEmpty)) {
        historyContainer.appendChild(historyEmpty);
      }
      if (historyContainer.contains(historyList)) {
        historyContainer.removeChild(historyList);
      }
      return;
    }

    if (historyContainer.contains(historyEmpty)) {
      historyContainer.removeChild(historyEmpty);
    }
    if (!historyContainer.contains(historyList)) {
      historyContainer.appendChild(historyList);
    }

    historyEntries.forEach((entry) => {
      const item = createElement("li", "history-item");
      const meta = createElement("div", "history-meta");
      const inputLabel = createElement("span", "history-input", entry.input);
      const timeLabel = createElement("time", "history-time", entry.timestamp);
      meta.appendChild(inputLabel);
      meta.appendChild(timeLabel);

      const answer = createElement("pre", "history-answer");
      const answerText =
        typeof entry.answer === "string"
          ? entry.answer
          : JSON.stringify(entry.answer, null, 2);
      answer.textContent = answerText;

      item.appendChild(meta);
      item.appendChild(answer);
      historyList.appendChild(item);
    });
  };

  historyContainer.appendChild(historyTitle);
  historyContainer.appendChild(historyEmpty);

  const resultWrapper = createElement("div", "dialog-result");
  resultWrapper.appendChild(output);
  resultWrapper.appendChild(historyContainer);

  form.appendChild(input);
  form.appendChild(submit);
  container.appendChild(form);
  container.appendChild(resultWrapper);

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const expr = input.value.trim();
    if (!expr) return;
    const payload = { digits: digitsFromExpression(expr) };
    output.textContent = "Загрузка...";
    try {
      const res = await fetch("/dialog", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      const data = await res.json();
      output.textContent = JSON.stringify(data, null, 2);
      historyEntries.unshift({
        input: expr,
        answer: data,
        timestamp: new Date().toLocaleString()
      });
      renderHistory();
    } catch (err) {
      output.textContent = `Ошибка: ${String(err)}`;
    }
  });
}

function renderStatus(container: HTMLElement) {
  const panel = createElement("div", "panel");
  const button = createElement("button", "", "Обновить статус");
  const pre = createElement("pre", "output");
  panel.appendChild(button);
  container.appendChild(panel);
  container.appendChild(pre);

  button.addEventListener("click", async () => {
    pre.textContent = "Загрузка...";
    try {
      const res = await fetch("/status");
      const json = await res.json();
      pre.textContent = JSON.stringify(json, null, 2);
    } catch (err) {
      pre.textContent = `Ошибка: ${String(err)}`;
    }
  });
}

function renderMemory(container: HTMLElement) {
  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.placeholder = "Префикс (цифры)";
  const submit = document.createElement("button");
  submit.type = "submit";
  submit.textContent = "Найти";
  const pre = createElement("pre", "output");

  form.appendChild(input);
  form.appendChild(submit);
  container.appendChild(form);
  container.appendChild(pre);

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const key = input.value.trim();
    if (!key) return;
    pre.textContent = "Загрузка...";
    try {
      const res = await fetch(`/fkv/prefix?key=${encodeURIComponent(key)}&k=5`);
      const json = await res.json();
      pre.textContent = JSON.stringify(json, null, 2);
    } catch (err) {
      pre.textContent = `Ошибка: ${String(err)}`;
    }
  });
}

function renderPlaceholder(container: HTMLElement, text: string) {
  const para = createElement("p", "placeholder", text);
  container.appendChild(para);
}

function renderPrograms(container: HTMLElement) {
  renderPlaceholder(container, "Список программ появится на этапе B.");
}

function renderSynth(container: HTMLElement) {
  renderPlaceholder(container, "Синтез будет реализован в следующем инкременте.");
}

function renderChain(container: HTMLElement) {
  renderPlaceholder(container, "Блокчейн будет подключен позже.");
}

function renderCluster(container: HTMLElement) {
  renderPlaceholder(container, "Кластерные метрики будут доступны в будущем.");
}

const renderers: Record<string, (container: HTMLElement) => void> = {
  dialog: renderDialog,
  memory: renderMemory,
  programs: renderPrograms,
  synth: renderSynth,
  chain: renderChain,
  status: renderStatus,
  cluster: renderCluster
};

function mountApp() {
  const app = document.getElementById("app");
  if (!app) return;

  const wrapper = createElement("div", "app-wrapper");
  const nav = createElement("nav", "tabs");
  const content = createElement("section", "content");

  tabs.forEach((tab, idx) => {
    const btn = createElement("button", idx === 0 ? "tab active" : "tab", tab.label);
    btn.addEventListener("click", () => {
      nav.querySelectorAll(".tab").forEach((el) => el.classList.remove("active"));
      btn.classList.add("active");
      content.innerHTML = "";
      const renderer = renderers[tab.id];
      renderer(content);
    });
    nav.appendChild(btn);
  });

  wrapper.appendChild(nav);
  wrapper.appendChild(content);
  app.appendChild(wrapper);

  renderers["dialog"](content);
}

function injectStyles() {
  const style = document.createElement("style");
  style.textContent = `
    body { font-family: system-ui, sans-serif; margin: 0; background: #111; color: #f5f5f5; }
    .app-wrapper { display: flex; flex-direction: column; height: 100vh; }
    .tabs { display: flex; gap: 8px; padding: 12px; background: #1b1b1b; }
    .tab { background: #2c2c2c; border: none; color: #f5f5f5; padding: 8px 14px; cursor: pointer; border-radius: 4px; }
    .tab.active { background: #3f64ff; }
    .content { flex: 1; padding: 16px; overflow-y: auto; }
    .panel { display: flex; gap: 12px; margin-bottom: 16px; }
    input { flex: 1; padding: 8px; border-radius: 4px; border: 1px solid #333; background: #222; color: #f5f5f5; }
    button { padding: 8px 14px; border-radius: 4px; border: none; background: #3f64ff; color: white; cursor: pointer; }
    pre.output { background: #000; padding: 12px; border-radius: 4px; min-height: 160px; overflow-x: auto; }
    .placeholder { opacity: 0.7; }
    .dialog-result { display: flex; gap: 16px; align-items: flex-start; }
    .dialog-result .output { flex: 1; }
    .history-container { width: 320px; background: #181818; border-radius: 6px; padding: 12px; display: flex; flex-direction: column; gap: 12px; }
    .history-title { margin: 0; font-size: 1rem; }
    .history-empty { margin: 0; opacity: 0.6; font-size: 0.9rem; }
    .history-list { list-style: none; margin: 0; padding: 0; border: 1px solid #222; border-radius: 6px; overflow: hidden; }
    .history-item { padding: 10px 12px; background: #111; display: flex; flex-direction: column; gap: 6px; }
    .history-item:nth-child(odd) { background: #161616; }
    .history-meta { display: flex; justify-content: space-between; gap: 12px; font-size: 0.82rem; color: #a0a0a0; }
    .history-input { font-weight: 600; color: #f5f5f5; }
    .history-answer { margin: 0; background: rgba(255, 255, 255, 0.05); padding: 8px; border-radius: 4px; font-size: 0.85rem; white-space: pre-wrap; word-break: break-word; }
    .history-time { font-variant-numeric: tabular-nums; }
    @media (max-width: 900px) {
      .dialog-result { flex-direction: column; }
      .history-container { width: 100%; }
    }
  `;
  document.head.appendChild(style);
}

injectStyles();
mountApp();
