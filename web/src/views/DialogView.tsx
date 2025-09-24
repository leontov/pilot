// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useMemo, useState } from "react";
import { apiClient, formatApiError } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";
import { SkeletonLines } from "../components/Skeleton";
import { TraceViewer } from "../components/TraceViewer";

interface HistoryEntry {
  id: string;
  input: string;
  timestampIso: string;
  answer: string;
  trace?: unknown;
}

type HistoryTimeFilter = "all" | "5m" | "30m" | "1h" | "day";

const TIME_FILTERS: Array<{ value: HistoryTimeFilter; label: string }> = [
  { value: "all", label: "За всё время" },
  { value: "5m", label: "Последние 5 мин" },
  { value: "30m", label: "Последние 30 мин" },
  { value: "1h", label: "Последний час" },
  { value: "day", label: "Последние 24 часа" }
];

const FILTER_TO_MINUTES: Record<Exclude<HistoryTimeFilter, "all">, number> = {
  "5m": 5,
  "30m": 30,
  "1h": 60,
  day: 60 * 24
};

function formatTimestamp(iso: string) {
  const date = new Date(iso);
  return date.toLocaleString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    day: "2-digit",
    month: "2-digit"
  });
}

function escapeRegExp(value: string) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function highlightMatch(text: string, query: string) {
  if (!query.trim()) {
    return text;
  }
  const pattern = new RegExp(`(${escapeRegExp(query)})`, "gi");
  return text.split(pattern).map((part, index) =>
    index % 2 === 1 ? (
      <span key={`${part}-${index}`} className="history-highlight">
        {part}
      </span>
    ) : (
      <span key={`${part}-${index}`}>{part}</span>
    )
  );
}

function createHistoryId() {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2, 10);
}

function digitsFromExpression(expr: string): number[] {
  const cleaned = expr.replace(/\s+/g, "");
  const result: number[] = [];
  for (const char of cleaned) {
    if (char >= "0" && char <= "9") {
      result.push(Number(char));
      continue;
    }
    const code = char.codePointAt(0);
    if (typeof code === "number") {
      result.push(code);
    }
  }
  return result;
}

export function DialogView() {
  const { notify } = useNotifications();
  const [input, setInput] = useState("2+2");
  const [isLoading, setIsLoading] = useState(false);
  const [answer, setAnswer] = useState<string | null>(null);
  const [trace, setTrace] = useState<unknown>(null);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [searchTerm, setSearchTerm] = useState("");
  const [timeFilter, setTimeFilter] = useState<HistoryTimeFilter>("all");

  const digitsPreview = useMemo(() => digitsFromExpression(input), [input]);
  const digitAnalytics = useMemo(() => {
    const counts = Array.from({ length: 10 }, () => 0);
    for (const entry of history) {
      for (const source of [entry.input, entry.answer]) {
        for (const char of source) {
          if (char >= "0" && char <= "9") {
            counts[Number(char)] += 1;
          }
        }
      }
    }
    const total = counts.reduce((sum, value) => sum + value, 0);
    const max = counts.reduce((acc, value) => Math.max(acc, value), 0);
    return { counts, total, max };
  }, [history]);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!input.trim()) {
      notify({ title: "Введите запрос", message: "Поле ввода не должно быть пустым", type: "error", timeout: 4000 });
      return;
    }

    setIsLoading(true);
    try {
      const response = await apiClient.dialog({ input: input.trim() });
      const safeAnswer = typeof response.answer === "string" ? response.answer : JSON.stringify(response.answer);
      setAnswer(safeAnswer);
      setTrace(response.trace ?? null);
      const timestampIso = response.timestamp ?? new Date().toISOString();
      const entry: HistoryEntry = {
        id: createHistoryId(),
        input: input.trim(),
        answer: safeAnswer,
        trace: response.trace,
        timestampIso
      };
      setHistory((prev) => [entry, ...prev].slice(0, 20));
      notify({ title: "Диалог выполнен", message: "Ответ получен от ядра Kolibri Ω", type: "success", timeout: 3500 });
    } catch (error) {
      const message = formatApiError(error);
      notify({ title: "Ошибка диалога", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  const filteredHistory = useMemo(() => {
    if (!history.length) {
      return history;
    }
    const query = searchTerm.trim().toLowerCase();
    const now = Date.now();
    return history.filter((entry) => {
      const matchesSearch = query
        ? `${entry.input} ${entry.answer}`.toLowerCase().includes(query)
        : true;
      if (!matchesSearch) {
        return false;
      }
      if (timeFilter === "all") {
        return true;
      }
      const minutesLimit = FILTER_TO_MINUTES[timeFilter];
      const entryDate = new Date(entry.timestampIso).getTime();
      if (Number.isNaN(entryDate)) {
        return true;
      }
      const diffMinutes = (now - entryDate) / (1000 * 60);
      return diffMinutes <= minutesLimit;
    });
  }, [history, searchTerm, timeFilter]);

  const historySummary = useMemo(() => {
    if (!history.length) {
      return "История пока пуста";
    }
    const filtersActive = timeFilter !== "all" || searchTerm.trim().length > 0;
    const base = `Показано ${filteredHistory.length} из ${history.length}`;
    return filtersActive ? `${base}. Применены фильтры.` : base;
  }, [filteredHistory.length, history.length, searchTerm, timeFilter]);

  return (
    <section className="view" aria-labelledby="dialog-tab">
      <article className="panel">
        <header>
          <h2>Диалог с Δ-VM</h2>
          <p>Введите формулу или запрос, чтобы получить ответ и трассировку выполнения.</p>
        </header>
        <form className="form-grid" onSubmit={handleSubmit}>
          <label htmlFor="dialog-input">Запрос</label>
          <input
            id="dialog-input"
            name="dialog"
            value={input}
            onChange={(event) => setInput(event.target.value)}
            placeholder="Например: 2+2"
            autoComplete="off"
            disabled={isLoading}
          />
          <div className="inline-actions">
            <button type="submit" disabled={isLoading}>
              {isLoading ? <Spinner /> : "Отправить"}
            </button>
            <span className="badge" aria-live="polite">
              Цифры: {digitsPreview.length ? digitsPreview.join(", ") : "—"}
            </span>
          </div>
        </form>
        <div className="stack">
          <div className="trace-block">
            <h3 className="trace-title">Ответ</h3>
            <pre className="output" aria-live="polite" aria-busy={isLoading}>
              {isLoading ? <SkeletonLines count={3} /> : answer ?? "Ожидаем запрос"}
            </pre>
          </div>
          <div className="trace-block">
            <h3 className="trace-title">Трассировка</h3>
            {isLoading ? <SkeletonLines count={4} /> : <TraceViewer trace={trace} />}
          </div>
        </div>
      </article>
      <article className="panel">
        <header>
          <h3>История диалогов</h3>
          <p>Сохраняем последние 20 обращений к ядру для быстрого повторения сценариев.</p>
        </header>
        <div className="history-controls">
          <label className="history-search" htmlFor="history-search">
            <span>Поиск</span>
            <input
              id="history-search"
              type="search"
              placeholder="Например: трассировка"
              value={searchTerm}
              onChange={(event) => setSearchTerm(event.target.value)}
            />
          </label>
          <label className="history-search" htmlFor="history-filter">
            <span>Период</span>
            <select
              id="history-filter"
              value={timeFilter}
              onChange={(event) => setTimeFilter(event.target.value as HistoryTimeFilter)}
            >
              {TIME_FILTERS.map((filter) => (
                <option key={filter.value} value={filter.value}>
                  {filter.label}
                </option>
              ))}
            </select>
          </label>
        </div>
        <p className="history-summary" aria-live="polite">
          {historySummary}
        </p>
        {history.length === 0 ? (
          <div className="empty-state">История пуста. Отправьте первый запрос.</div>
        ) : filteredHistory.length === 0 ? (
          <div className="empty-state">Нет записей, подходящих под текущие фильтры.</div>
        ) : (
          <ul className="history-list">
            {filteredHistory.map((entry) => (
              <li key={entry.id} className="history-item">
                <header>
                  <span>{highlightMatch(entry.input, searchTerm)}</span>
                  <time dateTime={entry.timestampIso}>{formatTimestamp(entry.timestampIso)}</time>
                </header>
                <p>{highlightMatch(entry.answer, searchTerm)}</p>
              </li>
            ))}
          </ul>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Аналитика цифр</h3>
          <p>Частотность десятичных символов в последних сообщениях.</p>
        </header>
        {digitAnalytics.total === 0 ? (
          <div className="empty-state">Данные появятся после выполнения запросов.</div>
        ) : (
          <div className="digit-analytics">
            {digitAnalytics.counts.map((value, digit) => {
              const percent = digitAnalytics.max > 0 ? Math.round((value / digitAnalytics.max) * 100) : 0;
              return (
                <div key={digit} className="digit-analytics__row">
                  <span className="digit-analytics__digit">{digit}</span>
                  <div className="digit-bar" aria-hidden="true">
                    <div className="digit-bar__value" style={{ width: `${percent}%` }} />
                  </div>
                  <span className="digit-analytics__count">{value}</span>
                </div>
              );
            })}
          </div>
        )}
      </article>
    </section>
  );
}
