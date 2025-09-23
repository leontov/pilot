
import "./main.css";

const tabs = [
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
  const spinner = createSpinner();
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
  form.appendChild(spinner);
  container.appendChild(form);


  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const expr = input.value.trim();
    if (!expr) return;
    const payload = { digits: digitsFromExpression(expr) };
    submit.disabled = true;
    spinner.classList.remove("hidden");
    output.textContent = "Загрузка...";
    traceContainer.innerHTML = "";
    try {
      const res = await fetch("/api/v1/dialog", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }

    } catch (err) {

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
  const spinner = createSpinner();
  const pre = createElement("pre", "output");

  form.appendChild(input);
  form.appendChild(submit);
  form.appendChild(spinner);
  container.appendChild(form);
  container.appendChild(pre);

  form.addEventListener("submit", async (ev) => {
    ev.preventDefault();
    const key = input.value.trim();
    if (!key) return;
    pre.textContent = "Загрузка...";
    submit.disabled = true;
    spinner.classList.remove("hidden");
    try {

    } catch (err) {

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
  status: renderStatus,
  cluster: renderCluster
};

function mountApp() {
  const app = document.getElementById("app");
  if (!app) return;

  const wrapper = createElement("div", "app-wrapper");

  const topBar = createElement("header", "top-bar");
  const nav = createElement("nav", "tabs");
  nav.setAttribute("aria-label", "Основные разделы");
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
    const btn = createElement("button", idx === 0 ? "tab active" : "tab", tab.label) as HTMLButtonElement;
    btn.type = "button";
    btn.addEventListener("click", () => {
      nav.querySelectorAll<HTMLButtonElement>(".tab").forEach((el) => el.classList.remove("active"));
      btn.classList.add("active");
      content.innerHTML = "";
      content.className = "content";
      const renderer = renderers[tab.id];
      renderer(content);
      content.setAttribute("data-view", tab.id);
    });
    nav.appendChild(btn);
  });

  topBar.appendChild(nav);
  topBar.appendChild(actions);
  wrapper.appendChild(topBar);

  wrapper.appendChild(header);

  wrapper.appendChild(content);
  app.appendChild(wrapper);

  renderers["dialog"](content);
  content.setAttribute("data-view", "dialog");
}


mountApp();
