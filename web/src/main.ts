const tabs = [
  { id: "dialog", label: "Диалог" },
  { id: "memory", label: "Память" },
  { id: "programs", label: "Программы" },
  { id: "synth", label: "Синтез" },
  { id: "chain", label: "Блокчейн" },
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

function parseProgramInput(source: string): number[] {
  const cleaned = source.replace(/[\[\]]/g, " ");
  const tokens = cleaned.split(/[\s,]+/).filter(Boolean);
  if (tokens.length === 0) {
    throw new Error("Введите хотя бы один байт программы");
  }
  const result: number[] = [];
  for (const token of tokens) {
    if (token.trim() === "") {
      continue;
    }
    const value = Number(token);
    if (!Number.isInteger(value) || value < 0 || value > 255) {
      throw new Error(`Некорректное значение байта: "${token}"`);
    }
    result.push(value);
  }
  if (result.length === 0) {
    throw new Error("Не удалось распознать ни одного байта");
  }
  return result;
}

function renderDialog(container: HTMLElement) {
  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.type = "text";
  input.placeholder = "Введите выражение (например, 2+2)";
  const submit = document.createElement("button");
  submit.type = "submit";
  submit.textContent = "Выполнить";
  const output = createElement("pre", "output");

  form.appendChild(input);
  form.appendChild(submit);
  container.appendChild(form);
  container.appendChild(output);

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
      const data = await res.json();
      output.textContent = JSON.stringify(data, null, 2);
    } catch (err) {
      output.textContent = `Ошибка: ${String(err)}`;
    }
  });
}

function renderStatus(container: HTMLElement) {
  const button = createElement("button", "refresh", "Обновить статус");
  const pre = createElement("pre", "output");
  container.appendChild(button);
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
  const form = createElement("form", "program-form") as HTMLFormElement;
  const textarea = document.createElement("textarea");
  textarea.placeholder = "Введите байты программы, например: 16, 0, 0, 2";
  const submit = document.createElement("button");
  submit.type = "submit";
  submit.textContent = "Запустить";
  form.appendChild(textarea);
  form.appendChild(submit);

  const output = createElement("div", "program-output");
  const statusEl = createElement("div", "program-status", "Статус: —");
  const resultEl = createElement("div", "program-result", "Результат: —");
  const traceTitle = createElement("div", "program-trace-title", "Трасса:");
  const tracePre = createElement("pre", "output program-trace", "[]");

  output.appendChild(statusEl);
  output.appendChild(resultEl);
  output.appendChild(traceTitle);
  output.appendChild(tracePre);

  container.appendChild(form);
  container.appendChild(output);

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const raw = textarea.value.trim();
    if (!raw) {
      statusEl.textContent = "Статус: введите программу";
      resultEl.textContent = "Результат: —";
      tracePre.textContent = "[]";
      return;
    }

    let program: number[];
    try {
      program = parseProgramInput(raw);
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      statusEl.textContent = `Статус: ${message}`;
      resultEl.textContent = "Результат: —";
      tracePre.textContent = "[]";
      return;
    }

    statusEl.textContent = "Статус: выполнение...";
    resultEl.textContent = "Результат: —";
    tracePre.textContent = "[]";

    try {
      const res = await fetch("/run", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ program })
      });
      const data = await res.json();
      if ("error" in data) {
        statusEl.textContent = `Статус: ошибка — ${String(data.error)}`;
        resultEl.textContent = "Результат: —";
        tracePre.textContent = "[]";
        return;
      }
      const statusText = typeof data.status !== "undefined" ? String(data.status) : "—";
      const stepsText = typeof data.steps !== "undefined" ? `, шаги: ${String(data.steps)}` : "";
      statusEl.textContent = `Статус: ${statusText}${stepsText}`;
      const resultText = typeof data.result !== "undefined" ? String(data.result) : "—";
      resultEl.textContent = `Результат: ${resultText}`;
      const traceData = data.trace;
      if (Array.isArray(traceData)) {
        tracePre.textContent = JSON.stringify(traceData, null, 2);
      } else if (typeof traceData !== "undefined") {
        tracePre.textContent = JSON.stringify(traceData, null, 2);
      } else {
        tracePre.textContent = "[]";
      }
    } catch (err) {
      statusEl.textContent = `Статус: ошибка запроса — ${String(err)}`;
      resultEl.textContent = "Результат: —";
      tracePre.textContent = "[]";
    }
  });
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
    textarea { width: 100%; padding: 10px; border-radius: 4px; border: 1px solid #333; background: #222; color: #f5f5f5; font-family: monospace; min-height: 140px; resize: vertical; }
    .program-form { display: flex; flex-direction: column; gap: 12px; margin-bottom: 16px; }
    .program-form button { align-self: flex-start; }
    .program-output { display: flex; flex-direction: column; gap: 8px; }
    .program-status, .program-result, .program-trace-title { background: #1b1b1b; padding: 8px 10px; border-radius: 4px; }
    .program-trace { max-height: 320px; overflow-y: auto; }
  `;
  document.head.appendChild(style);
}

injectStyles();
mountApp();
