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

type TraceEntry = {
  step?: number;
  ip?: number | string;
  opcode?: string;
  stack?: unknown;
  gas?: number | string;
  [key: string]: unknown;
};

function buildTraceTable(trace: TraceEntry[]): HTMLElement {
  const table = createElement("table", "trace-table") as HTMLTableElement;
  const thead = document.createElement("thead");
  const headerRow = document.createElement("tr");
  const headers: (keyof TraceEntry | "step")[] = [
    "step",
    "ip",
    "opcode",
    "stack",
    "gas"
  ];

  headers.forEach((key) => {
    const th = document.createElement("th");
    th.textContent = key;
    headerRow.appendChild(th);
  });

  thead.appendChild(headerRow);
  table.appendChild(thead);

  const tbody = document.createElement("tbody");
  trace.forEach((entry, idx) => {
    const tr = document.createElement("tr");
    headers.forEach((key) => {
      const td = document.createElement("td");
      const record = entry as Record<string, unknown>;
      const rawValue =
        key === "step" ? record[key] ?? idx : record[key as string];
      let value: string;
      if (rawValue === undefined || rawValue === null) {
        value = "";
      } else if (Array.isArray(rawValue) || typeof rawValue === "object") {
        try {
          value = JSON.stringify(rawValue);
        } catch (err) {
          value = String(rawValue);
        }
      } else {
        value = String(rawValue);
      }
      td.textContent = value;
      tr.appendChild(td);
    });
    tbody.appendChild(tr);
  });

  table.appendChild(tbody);
  return table;
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
  const traceContainer = createElement("div", "trace-container");

  form.appendChild(input);
  form.appendChild(submit);
  container.appendChild(form);
  container.appendChild(output);
  container.appendChild(traceContainer);

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const expr = input.value.trim();
    if (!expr) return;
    const payload = { digits: digitsFromExpression(expr) };
    output.textContent = "Загрузка...";
    traceContainer.innerHTML = "";
    try {
      const res = await fetch("/dialog", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const data = await res.json();
      const trace = Array.isArray(data.trace) ? data.trace : null;
      output.textContent = JSON.stringify(data, null, 2);
      if (trace && trace.length > 0) {
        const title = createElement("h3", "trace-title", "Трассировка");
        traceContainer.appendChild(title);
        traceContainer.appendChild(buildTraceTable(trace));
      }
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
    pre.output { background: #000; padding: 12px; border-radius: 4px; min-height: 160px; overflow-x: auto; margin-bottom: 12px; }
    .placeholder { opacity: 0.7; }
    .trace-container { margin-top: 8px; }
    .trace-title { margin: 0 0 8px; font-size: 16px; font-weight: 600; }
    .trace-table { width: 100%; border-collapse: collapse; background: #141414; border-radius: 4px; overflow: hidden; }
    .trace-table th { background: #2c2c2c; text-transform: uppercase; font-size: 12px; letter-spacing: 0.08em; color: #dcdcdc; }
    .trace-table th, .trace-table td { padding: 8px; border-bottom: 1px solid #2a2a2a; text-align: left; font-family: ui-monospace, SFMono-Regular, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace; }
    .trace-table tr:nth-child(even) td { background: #101010; }
    .trace-table tr:last-child td { border-bottom: none; }
  `;
  document.head.appendChild(style);
}

injectStyles();
mountApp();
