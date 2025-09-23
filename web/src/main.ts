import "./styles.css";

type TabId =
  | "dialog"
  | "memory"
  | "programs"
  | "synth"
  | "chain"
  | "status"
  | "cluster";

const tabs: { id: TabId; label: string }[] = [
  { id: "dialog", label: "Диалог" },
  { id: "memory", label: "Память" },
  { id: "programs", label: "Программы" },
  { id: "synth", label: "Синтез" },
  { id: "chain", label: "Блокчейн" },
  { id: "status", label: "Статус" },
  { id: "cluster", label: "Кластер" }
];

type ThemeMode = "light" | "dark";

const THEME_STORAGE_KEY = "kolibri-theme";

function readStoredTheme(): ThemeMode | null {
  try {
    const stored = localStorage.getItem(THEME_STORAGE_KEY);
    if (stored === "light" || stored === "dark") {
      return stored;
    }
  } catch (error) {
    console.warn("Не удалось прочитать тему из localStorage", error);
  }
  return null;
}

function systemPreferredTheme(): ThemeMode {
  return window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
}

function applyTheme(theme: ThemeMode) {
  document.documentElement.setAttribute("data-theme", theme);
  document.body.dataset.theme = theme;
  document.body.classList.remove("theme-light", "theme-dark");
  document.body.classList.add(theme === "dark" ? "theme-dark" : "theme-light");
  try {
    localStorage.setItem(THEME_STORAGE_KEY, theme);
  } catch (error) {
    console.warn("Не удалось сохранить тему", error);
  }
}

function initializeTheme(): ThemeMode {
  const preferred = readStoredTheme() ?? systemPreferredTheme();
  applyTheme(preferred);
  return preferred;
}

let activeTheme: ThemeMode = initializeTheme();

function toggleTheme(): ThemeMode {
  const nextTheme: ThemeMode = activeTheme === "dark" ? "light" : "dark";
  activeTheme = nextTheme;
  applyTheme(nextTheme);
  return nextTheme;
}

function createElement<T extends keyof HTMLElementTagNameMap>(
  tag: T,
  className?: string,
  text?: string
): HTMLElementTagNameMap[T] {
  const el = document.createElement(tag);
  if (className) el.className = className;
  if (text) el.textContent = text;
  return el;
}

function createSpinner(): HTMLSpanElement {
  const spinner = document.createElement("span");
  spinner.className = "spinner";
  spinner.textContent = "⏳";
  spinner.style.display = "none";
  spinner.setAttribute("aria-hidden", "true");
  return spinner;
}

function formatTimestamp(date: Date): string {
  return date.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
}

function safeJson(value: unknown): string {
  try {
    return JSON.stringify(value, null, 2);
  } catch (error) {
    console.warn("Не удалось сериализовать JSON", error);
    return String(value);
  }
}

function extractErrorMessage(data: unknown, fallback: string): string {
  if (typeof data === "object" && data !== null && "error" in data) {
    const errorValue = (data as { error?: unknown }).error;
    if (typeof errorValue === "string") {
      return errorValue;
    }
  }
  return fallback;
}

function readArrayField(data: unknown, field: string): unknown[] {
  if (typeof data === "object" && data !== null && field in data) {
    const value = (data as Record<string, unknown>)[field];
    if (Array.isArray(value)) {
      return value;
    }
  }
  return [];
}

function parseProgramInput(raw: string): number[] {
  const tokens = raw
    .split(/[,\s]+/)
    .map((token) => token.trim())
    .filter(Boolean);
  if (tokens.length === 0) {
    throw new Error("Введите байткод программы через пробел или запятую");
  }
  return tokens.map((token) => {
    const value = Number(token);
    if (!Number.isInteger(value) || value < 0 || value > 255) {
      throw new Error(`Недопустимый байт: "${token}"`);
    }
    return value;
  });
}

function renderDialog(container: HTMLElement) {
  const historyEntries: { input: string; answer: unknown; timestamp: string }[] = [];

  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.type = "text";
  input.placeholder = "Введите выражение (например, 2+2)";
  input.autocomplete = "off";
  const submit = createElement("button") as HTMLButtonElement;
  submit.type = "submit";
  submit.textContent = "Выполнить";
  const spinner = createSpinner();
  const output = createElement("pre", "output");
  output.textContent = "Ответ появится здесь";
  const traceContainer = createElement("div", "trace-container");
  const traceTitle = createElement("h3", "trace-title", "Трасса Δ-VM");
  const tracePre = createElement("pre", "output trace-output", "—");
  traceContainer.appendChild(traceTitle);
  traceContainer.appendChild(tracePre);

  const historyContainer = createElement("div", "history-container");
  const historyTitle = createElement("h3", "history-title", "История");
  const historyList = createElement("ul", "history-list");
  const historyEmpty = createElement("p", "history-empty", "Диалогов пока нет.");
  historyList.style.display = "none";

  const renderHistory = () => {
    historyList.innerHTML = "";
    if (historyEntries.length === 0) {
      historyEmpty.style.display = "block";
      historyList.style.display = "none";
      return;
    }
    historyEmpty.style.display = "none";
    historyList.style.display = "block";
    historyEntries.forEach((entry) => {
      const item = createElement("li", "history-item");
      const meta = createElement("div", "history-meta");
      const inputLabel = createElement("span", "history-input", entry.input);
      const timeLabel = createElement("time", "history-time", entry.timestamp);
      meta.appendChild(inputLabel);
      meta.appendChild(timeLabel);
      const answerBlock = createElement("pre", "history-answer", safeJson(entry.answer));
      item.appendChild(meta);
      item.appendChild(answerBlock);
      historyList.appendChild(item);
    });
  };

  historyContainer.appendChild(historyTitle);
  historyContainer.appendChild(historyEmpty);
  historyContainer.appendChild(historyList);

  const resultWrapper = createElement("div", "dialog-result");
  resultWrapper.appendChild(output);
  resultWrapper.appendChild(traceContainer);
  resultWrapper.appendChild(historyContainer);

  form.appendChild(input);
  form.appendChild(submit);
  form.appendChild(spinner);
  container.appendChild(form);
  container.appendChild(resultWrapper);

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const expr = input.value.trim();
    if (!expr) {
      return;
    }
    const payload = { input: expr };
    submit.disabled = true;
    spinner.style.display = "inline-flex";
    output.textContent = "Загрузка...";
    tracePre.textContent = "—";

    try {
      const response = await fetch("/api/v1/dialog", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      const data: unknown = await response.json().catch(() => ({}));
      if (!response.ok) {
        const message = extractErrorMessage(data, response.statusText);
        output.textContent = `Ошибка: ${message}`;
        return;
      }
      const record = typeof data === "object" && data !== null ? (data as Record<string, unknown>) : undefined;
      const answer = record && "answer" in record ? record.answer : data;
      output.textContent = typeof answer === "string" ? answer : safeJson(answer);
      const trace = record && "trace" in record ? record.trace : undefined;
      if (trace) {
        tracePre.textContent = safeJson(trace);
      } else {
        tracePre.textContent = "—";
      }
      historyEntries.unshift({ input: expr, answer, timestamp: formatTimestamp(new Date()) });
      if (historyEntries.length > 10) {
        historyEntries.length = 10;
      }
      renderHistory();
    } catch (error) {
      output.textContent = `Ошибка: ${String(error)}`;
    } finally {
      submit.disabled = false;
      spinner.style.display = "none";
    }
  });
}

function renderStatus(container: HTMLElement) {
  const panel = createElement("div", "panel");
  const button = createElement("button", "button-primary", "Обновить статус") as HTMLButtonElement;
  button.type = "button";
  const pre = createElement("pre", "output");
  pre.textContent = "Нажмите, чтобы получить статус узла";
  const spinner = createSpinner();
  panel.appendChild(button);
  panel.appendChild(spinner);
  container.appendChild(panel);
  container.appendChild(pre);
  container.classList.add("split-view");

  button.addEventListener("click", async () => {
    button.disabled = true;
    spinner.style.display = "inline-flex";
    pre.textContent = "Загрузка...";
    try {
      const response = await fetch("/api/v1/health");
      const data: unknown = await response.json().catch(() => ({}));
      if (!response.ok) {
        const message = extractErrorMessage(data, response.statusText);
        pre.textContent = `Ошибка: ${message}`;
        return;
      }
      pre.textContent = safeJson(data);
    } catch (error) {
      pre.textContent = `Ошибка: ${String(error)}`;
    } finally {
      button.disabled = false;
      spinner.style.display = "none";
    }
  });
}

function renderMemory(container: HTMLElement) {
  const form = createElement("form", "panel form") as HTMLFormElement;
  const input = document.createElement("input");
  input.placeholder = "Префикс (цифры)";
  input.autocomplete = "off";
  const submit = createElement("button") as HTMLButtonElement;
  submit.type = "submit";
  submit.textContent = "Найти";
  const spinner = createSpinner();

  const message = createElement(
    "p",
    "memory-message",
    "Введите префикс и нажмите «Найти»."
  );
  const table = createElement("table", "memory-table");
  const thead = document.createElement("thead");
  const headRow = document.createElement("tr");
  headRow.appendChild(createElement("th", undefined, "Префикс"));
  headRow.appendChild(createElement("th", undefined, "Значение"));
  thead.appendChild(headRow);
  const tbody = document.createElement("tbody");
  table.appendChild(thead);
  table.appendChild(tbody);
  table.style.display = "none";

  const programsTitle = createElement("h3", "memory-programs-title", "Программы");
  const programsList = createElement("ul", "memory-programs");
  programsTitle.style.display = "none";
  programsList.style.display = "none";

  form.appendChild(input);
  form.appendChild(submit);
  form.appendChild(spinner);
  container.appendChild(form);
  container.appendChild(message);
  container.appendChild(table);
  container.appendChild(programsTitle);
  container.appendChild(programsList);

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const prefix = input.value.trim();
    if (!prefix) {
      return;
    }
    submit.disabled = true;
    spinner.style.display = "inline-flex";
    message.textContent = "Загрузка...";
    table.style.display = "none";
    programsTitle.style.display = "none";
    programsList.style.display = "none";

    try {
      const response = await fetch(`/api/v1/fkv/get?prefix=${encodeURIComponent(prefix)}&k=10`);
      const data: unknown = await response.json().catch(() => ({}));
      if (!response.ok) {
        const messageText = extractErrorMessage(data, response.statusText);
        message.textContent = `Ошибка: ${messageText}`;
        return;
      }
      const values = readArrayField(data, "values");
      const programs = readArrayField(data, "programs");

      tbody.innerHTML = "";
      values.forEach((entry) => {
        const row = document.createElement("tr");
        if (typeof entry === "object" && entry !== null) {
          const obj = entry as { key?: unknown; value?: unknown };
          row.appendChild(createElement("td", undefined, String(obj.key ?? "")));
          row.appendChild(createElement("td", "value", String(obj.value ?? "")));
        } else {
          row.appendChild(createElement("td", undefined, prefix));
          row.appendChild(createElement("td", "value", String(entry)));
        }
        tbody.appendChild(row);
      });

      programsList.innerHTML = "";
      programs.forEach((program) => {
        const item = createElement("li", "memory-program-item", safeJson(program));
        programsList.appendChild(item);
      });

      if (values.length > 0) {
        table.style.display = "table";
        message.textContent = "";
      } else {
        message.textContent = "Ничего не найдено для указанного префикса.";
      }

      if (programs.length > 0) {
        programsTitle.style.display = "block";
        programsList.style.display = "grid";
      }
    } catch (error) {
      message.textContent = `Ошибка: ${String(error)}`;
    } finally {
      submit.disabled = false;
      spinner.style.display = "none";
    }
  });
}

function renderPlaceholder(container: HTMLElement, text: string) {
  const para = createElement("p", "placeholder", text);
  container.appendChild(para);
}

function renderPrograms(container: HTMLElement) {
  const form = createElement("form", "program-form panel form") as HTMLFormElement;
  const textarea = document.createElement("textarea");
  textarea.placeholder = "Введите байты программы, например: 16, 0, 0, 2";
  const submit = createElement("button") as HTMLButtonElement;
  submit.type = "submit";
  submit.textContent = "Запустить";
  const spinner = createSpinner();

  form.appendChild(textarea);
  form.appendChild(submit);
  form.appendChild(spinner);

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

  form.addEventListener("submit", async (event) => {
    event.preventDefault();
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
    } catch (error) {
      statusEl.textContent = `Статус: ${(error as Error).message}`;
      resultEl.textContent = "Результат: —";
      tracePre.textContent = "[]";
      return;
    }

    submit.disabled = true;
    spinner.style.display = "inline-flex";
    statusEl.textContent = "Статус: выполнение...";
    resultEl.textContent = "Результат: —";
    tracePre.textContent = "[]";

    try {
      const response = await fetch("/api/v1/vm/run", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ program })
      });
      const data: unknown = await response.json().catch(() => ({}));
      if (!response.ok) {
        const message = extractErrorMessage(data, response.statusText);
        statusEl.textContent = `Статус: ошибка — ${message}`;
        return;
      }
      const record = typeof data === "object" && data !== null ? (data as Record<string, unknown>) : undefined;
      const statusValue = record && "status" in record ? record.status : undefined;
      const stepsValue = record && "steps" in record ? record.steps : undefined;
      const stepsSuffix = typeof stepsValue !== "undefined" ? `, шаги: ${String(stepsValue)}` : "";
      statusEl.textContent = `Статус: ${String(statusValue ?? "—")}${stepsSuffix}`;
      const resultValue = record && "result" in record ? record.result : undefined;
      resultEl.textContent = `Результат: ${String(resultValue ?? "—")}`;
      const trace = record && "trace" in record ? record.trace : undefined;
      tracePre.textContent = trace ? safeJson(trace) : "[]";
    } catch (error) {
      statusEl.textContent = `Статус: ошибка запроса — ${String(error)}`;
    } finally {
      submit.disabled = false;
      spinner.style.display = "none";
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

const renderers: Record<TabId, (container: HTMLElement) => void> = {
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

  const topBar = createElement("header", "top-bar");
  const nav = createElement("nav", "tabs");
  nav.setAttribute("aria-label", "Основные разделы Kolibri Studio");
  const actions = createElement("div", "top-bar__actions");
  const themeToggle = createElement("button", "theme-toggle") as HTMLButtonElement;
  themeToggle.type = "button";

  const updateThemeToggle = (theme: ThemeMode) => {
    const nextThemeLabel = theme === "dark" ? "светлую" : "тёмную";
    themeToggle.textContent = theme === "dark" ? "Светлая тема" : "Тёмная тема";
    themeToggle.setAttribute("aria-label", `Переключить на ${nextThemeLabel} тему`);
    themeToggle.setAttribute("aria-pressed", theme === "dark" ? "true" : "false");
  };

  themeToggle.addEventListener("click", () => {
    const next = toggleTheme();
    updateThemeToggle(next);
  });
  updateThemeToggle(activeTheme);

  actions.appendChild(themeToggle);

  const content = createElement("section", "content content-single");
  content.setAttribute("role", "region");

  tabs.forEach((tab, index) => {
    const btn = createElement("button", index === 0 ? "tab active" : "tab", tab.label) as HTMLButtonElement;
    btn.type = "button";
    btn.addEventListener("click", () => {
      nav.querySelectorAll<HTMLButtonElement>(".tab").forEach((el) => el.classList.remove("active"));
      btn.classList.add("active");
      content.innerHTML = "";
      content.className = "content";
      const renderer = renderers[tab.id];
      renderer(content);
      content.setAttribute("data-view", tab.id);
      if (tab.id === "status") {
        content.classList.add("split-view");
      }
    });
    nav.appendChild(btn);
  });

  topBar.appendChild(nav);
  topBar.appendChild(actions);
  wrapper.appendChild(topBar);
  wrapper.appendChild(content);
  app.appendChild(wrapper);

  renderers.dialog(content);
  content.setAttribute("data-view", "dialog");
}

mountApp();
