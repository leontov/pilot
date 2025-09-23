interface TraceEntry {
  step: number;
  ip: number;
  op: number;
  stack: number;
  gas: number;
}

interface RunResponse {
  status: number;
  steps: number;
  result: number;
  trace: TraceEntry[];
}

interface DialogResponse extends RunResponse {}

interface FkvEntry {
  key: string;
  value: string;
}

interface FkvResponse {
  entries: FkvEntry[];
}

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

function isTraceEntry(value: unknown): value is TraceEntry {
  if (typeof value !== "object" || value === null) {
    return false;
  }
  const candidate = value as Record<string, unknown>;
  return (
    typeof candidate.step === "number" &&
    typeof candidate.ip === "number" &&
    typeof candidate.op === "number" &&
    typeof candidate.stack === "number" &&
    typeof candidate.gas === "number"
  );
}

function isRunResponse(value: unknown): value is RunResponse {
  if (typeof value !== "object" || value === null) {
    return false;
  }
  const candidate = value as Record<string, unknown>;
  return (
    typeof candidate.status === "number" &&
    typeof candidate.steps === "number" &&
    typeof candidate.result === "number" &&
    Array.isArray(candidate.trace) &&
    candidate.trace.every(isTraceEntry)
  );
}

function isFkvEntry(value: unknown): value is FkvEntry {
  if (typeof value !== "object" || value === null) {
    return false;
  }
  const candidate = value as Record<string, unknown>;
  return typeof candidate.key === "string" && typeof candidate.value === "string";
}

function isFkvResponse(value: unknown): value is FkvResponse {
  if (typeof value !== "object" || value === null) {
    return false;
  }
  const candidate = value as Record<string, unknown>;
  return Array.isArray(candidate.entries) && candidate.entries.every(isFkvEntry);
}

function formatRunResponse(data: RunResponse): string {
  const lines = [
    `Статус: ${data.status}`,
    `Шаги: ${data.steps}`,
    `Результат: ${data.result}`
  ];
  if (data.trace.length === 0) {
    lines.push("Трассировка: отсутствует");
  } else {
    lines.push(
      "Трассировка:",
      ...data.trace.map(
        (entry) =>
          `  #${entry.step} ip=${entry.ip} op=${entry.op} stack=${entry.stack} gas=${entry.gas}`
      )
    );
  }
  return lines.join("\n");
}

function formatFkvResponse(data: FkvResponse): string {
  if (data.entries.length === 0) {
    return "Совпадений не найдено.";
  }
  return data.entries.map((entry) => `${entry.key} → ${entry.value}`).join("\n");
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
      if (!res.ok) {
        const errorBody: unknown = await res.json().catch(() => null);
        const message =
          errorBody &&
          typeof errorBody === "object" &&
          errorBody !== null &&
          "error" in errorBody &&
          typeof (errorBody as { error?: unknown }).error === "string"
            ? (errorBody as { error: string }).error
            : `HTTP ${res.status}`;
        throw new Error(message);
      }
      const json: unknown = await res.json();
      if (!isRunResponse(json)) {
        throw new Error("Некорректный ответ сервера");
      }
      const data: DialogResponse = json;
      output.textContent = formatRunResponse(data);
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
      if (!res.ok) {
        const errorBody: unknown = await res.json().catch(() => null);
        const message =
          errorBody &&
          typeof errorBody === "object" &&
          errorBody !== null &&
          "error" in errorBody &&
          typeof (errorBody as { error?: unknown }).error === "string"
            ? (errorBody as { error: string }).error
            : `HTTP ${res.status}`;
        throw new Error(message);
      }
      const json: unknown = await res.json();
      if (!isFkvResponse(json)) {
        throw new Error("Некорректный ответ сервера");
      }
      const data: FkvResponse = json;
      pre.textContent = formatFkvResponse(data);
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
    pre.output { background: #000; padding: 12px; border-radius: 4px; min-height: 160px; overflow-x: auto; }
    .placeholder { opacity: 0.7; }
  `;
  document.head.appendChild(style);
}

injectStyles();
mountApp();
