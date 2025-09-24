// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useMemo, useState } from "react";
import { apiClient, formatApiError } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

interface HistoryEntry {
  input: string;
  timestamp: string;
  answer: string;
  trace?: unknown;
}

function formatTimestamp(date: Date) {
  return date.toLocaleString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    day: "2-digit",
    month: "2-digit"
  });
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

function stringifyTrace(trace: unknown) {
  if (trace == null) {
    return "[]";
  }
  try {
    return JSON.stringify(trace, null, 2);
  } catch (error) {
    return String(trace);
  }
}

export function DialogView() {
  const { notify } = useNotifications();
  const [input, setInput] = useState("2+2");
  const [isLoading, setIsLoading] = useState(false);
  const [answer, setAnswer] = useState<string | null>(null);
  const [trace, setTrace] = useState<unknown>(null);
  const [history, setHistory] = useState<HistoryEntry[]>([]);

  const digitsPreview = useMemo(() => digitsFromExpression(input), [input]);

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
      const entry: HistoryEntry = {
        input: input.trim(),
        answer: safeAnswer,
        trace: response.trace,
        timestamp: response.timestamp ?? formatTimestamp(new Date())
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
            <pre className="output" aria-live="polite">
              {answer ?? "Ожидаем запрос"}
            </pre>
          </div>
          <div className="trace-block">
            <h3 className="trace-title">Трассировка</h3>
            <pre className="output" aria-live="polite">
              {stringifyTrace(trace)}
            </pre>
          </div>
        </div>
      </article>
      <article className="panel">
        <header>
          <h3>История диалогов</h3>
          <p>Сохраняем последние 20 обращений к ядру для быстрого повторения сценариев.</p>
        </header>
        {history.length === 0 ? (
          <div className="empty-state">История пуста. Отправьте первый запрос.</div>
        ) : (
          <ul className="history-list">
            {history.map((entry) => (
              <li key={`${entry.timestamp}-${entry.input}`} className="history-item">
                <header>
                  <span>{entry.input}</span>
                  <time dateTime={entry.timestamp}>{entry.timestamp}</time>
                </header>
                <p>{entry.answer}</p>
              </li>
            ))}
          </ul>
        )}
      </article>
    </section>
  );
}
