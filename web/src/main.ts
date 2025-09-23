
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
  const historyEntries: { input: string; answer: unknown; timestamp: string }[] = [];

  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.type = "text";
  input.placeholder = "Введите выражение (например, 2+2)";
  const submit = document.createElement("button");
  submit.type = "submit";
  submit.textContent = "Выполнить";
  const output = createElement("pre", "output");
  const traceContainer = createElement("div", "trace-container");

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
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }

    } catch (err) {
      showError(`Ошибка: ${String(err)}`);
    }
  });
}

function renderStatus(container: HTMLElement) {
  const panel = createElement("div", "panel");
  const button = createElement("button", "button-primary", "Обновить статус") as HTMLButtonElement;
  button.type = "button";
  const pre = createElement("pre", "output");
  panel.appendChild(button);
  container.appendChild(panel);
  container.appendChild(pre);
  container.classList.add("split-view");

  button.addEventListener("click", async () => {
    pre.textContent = "Загрузка...";
    try {
      const res = await fetch("/status");
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      const json: StatusResponse = await res.json();
      pre.textContent = [
        `Аптайм: ${json.uptime_ms} мс`,
        `VM max steps: ${json.vm_max_steps}`,
        `VM max stack: ${json.vm_max_stack}`,
        `Seed: ${json.seed}`
      ].join("\n");
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
        throw new Error(`HTTP ${res.status}`);
      }
      const json: FkvResponse = await res.json();
      if (!Array.isArray(json.entries) || json.entries.length === 0) {
        pre.textContent = "Совпадений не найдено.";
        return;
      }
      pre.textContent = json.entries
        .map((entry) => `${entry.key} → ${entry.value}`)
        .join("\n");
    } catch (err) {
      showError(`Ошибка: ${String(err)}`);
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
  const header = createElement("header", "app-header");
  const nav = createElement("nav", "tabs");
  nav.setAttribute("aria-label", "Основные разделы Kolibri Studio");
  const controls = createElement("div", "header-controls");
  const themeToggle = createElement("button", "theme-toggle") as HTMLButtonElement;
  themeToggle.type = "button";

  let theme = determineInitialTheme();
  applyTheme(theme);
  updateThemeToggle(themeToggle, theme);

  themeToggle.addEventListener("click", () => {
    theme = theme === "dark" ? "light" : "dark";
    applyTheme(theme);
    updateThemeToggle(themeToggle, theme);
  });

  controls.appendChild(themeToggle);
  header.appendChild(nav);
  header.appendChild(controls);

  const content = createElement("section", "content");
  content.setAttribute("role", "region");

  tabs.forEach((tab, idx) => {
    const btn = createElement("button", idx === 0 ? "tab active" : "tab", tab.label);
    btn.addEventListener("click", () => {
      nav.querySelectorAll(".tab").forEach((el) => el.classList.remove("active"));
      btn.classList.add("active");
      content.innerHTML = "";
      content.className = "content";
      const renderer = renderers[tab.id];
      renderer(content);
    });
    nav.appendChild(btn);
  });

  wrapper.appendChild(header);
  wrapper.appendChild(content);
  app.appendChild(wrapper);

  renderers["dialog"](content);
}

<
mountApp();
