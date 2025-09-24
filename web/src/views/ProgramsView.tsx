// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useEffect, useRef, useState } from "react";
import { apiClient, VmStreamSession, VmTraceEvent } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

function parseProgram(source: string): number[] {
  const tokens = source
    .split(/[^0-9-]+/)
    .map((token) => token.trim())
    .filter(Boolean);

  if (!tokens.length) {
    throw new Error("Введите хотя бы один байт программы");
  }

  const result = tokens.map((token) => {
    const value = Number(token);
    if (!Number.isInteger(value)) {
      throw new Error(`Неверный байт: ${token}`);
    }
    return value;
  });

  return result;
}

function formatTrace(trace: unknown) {
  if (trace == null) {
    return "[]";
  }
  try {
    return JSON.stringify(trace, null, 2);
  } catch (error) {
    return String(trace);
  }
}

export function ProgramsView() {
  const { notify } = useNotifications();
  const [programSource, setProgramSource] = useState("16,0,0,2");
  const [gasLimit, setGasLimit] = useState("256");
  const [isLoading, setIsLoading] = useState(false);
  const [status, setStatus] = useState<string | null>(null);
  const [result, setResult] = useState<unknown>(null);
  const [trace, setTrace] = useState<unknown>(null);
  const [editorSource, setEditorSource] = useState("// PUSHd 2, PUSHd 2, ADD10, HALT\n16 0 0 2");
  const [editorGasLimit, setEditorGasLimit] = useState("512");
  const [isStreaming, setIsStreaming] = useState(false);
  const [streamSessionId, setStreamSessionId] = useState<string | null>(null);
  const [streamEvents, setStreamEvents] = useState<VmTraceEvent[]>([]);
  const [streamError, setStreamError] = useState<string | null>(null);
  const streamSessionRef = useRef<VmStreamSession | null>(null);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    let program: number[];
    try {
      program = parseProgram(programSource);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка парсинга", message, type: "error" });
      return;
    }

    setIsLoading(true);
    setStatus(null);
    setResult(null);
    setTrace(null);

    try {
      const gas = gasLimit.trim() ? Number(gasLimit) : undefined;
      const response = await apiClient.runProgram({ program, gasLimit: Number.isFinite(gas) ? gas : undefined });
      setStatus(response.status ?? "Выполнено");
      setResult(typeof response.result === "undefined" ? "—" : response.result);
      setTrace(response.trace ?? null);
      notify({ title: "Программа выполнена", type: "success", timeout: 2500 });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка Δ-VM", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  const finalizeStream = () => {
    if (streamSessionRef.current) {
      streamSessionRef.current.close();
      streamSessionRef.current = null;
    }
    setIsStreaming(false);
  };

  const handleStream = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    let program: number[];
    try {
      program = parseProgram(editorSource);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка парсинга", message, type: "error" });
      return;
    }

    const gas = editorGasLimit.trim() ? Number(editorGasLimit) : undefined;
    const gasValue = Number.isFinite(gas) ? gas : undefined;

    setStreamEvents([]);
    setStreamError(null);
    setIsStreaming(true);
    try {
      streamSessionRef.current?.close();
      const session = await apiClient.streamVmExecution(
        { program, gasLimit: gasValue },
        {
          onEvent: (eventPayload) => {
            setStreamEvents((prev) => [...prev, eventPayload]);
            if (eventPayload.type === "error") {
              setStreamError(typeof eventPayload.payload === "string" ? eventPayload.payload : "Ошибка выполнения Δ-VM");
              finalizeStream();
            }
            if (eventPayload.type === "complete" || eventPayload.type === "result") {
              finalizeStream();
            }
          },
          onError: (error) => {
            setStreamError(error.message);
            finalizeStream();
          }
        }
      );
      streamSessionRef.current = session;
      setStreamSessionId(session.sessionId);
      notify({ title: "Стрим трассы запущен", message: `Сессия ${session.sessionId}`, type: "info" });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      setStreamError(message);
      setIsStreaming(false);
      notify({ title: "Не удалось запустить стрим", message, type: "error" });
    }
  };

  const stopStreaming = () => {
    finalizeStream();
    setStreamSessionId(null);
    notify({ title: "Стрим остановлен", type: "info", timeout: 2000 });
  };

  useEffect(() => {
    return () => {
      streamSessionRef.current?.close();
    };
  }, []);

  return (
    <section className="view" aria-labelledby="programs-tab">
      <article className="panel">
        <header>
          <h2>Δ-VM Runner</h2>
          <p>Используйте байткод программы, чтобы выполнить её в виртуальной машине и изучить трассировку.</p>
        </header>
        <form className="form-grid" onSubmit={handleSubmit}>
          <label htmlFor="program-source">Байткод (через запятую или пробел)</label>
          <textarea
            id="program-source"
            value={programSource}
            onChange={(event) => setProgramSource(event.target.value)}
            disabled={isLoading}
          />
          <label htmlFor="program-gas">Лимит газа (опционально)</label>
          <input
            id="program-gas"
            value={gasLimit}
            onChange={(event) => setGasLimit(event.target.value)}
            placeholder="Например: 1024"
            inputMode="numeric"
            disabled={isLoading}
          />
          <button type="submit" disabled={isLoading}>
            {isLoading ? <Spinner /> : "Выполнить"}
          </button>
        </form>
      </article>
      <article className="panel">
        <header>
          <h3>Результат выполнения</h3>
          <p>Статус, возвращаемое значение и трасса для отладки.</p>
        </header>
        <div className="stack">
          <div className="trace-block">
            <h4 className="trace-title">Статус</h4>
            <pre className="output" aria-live="polite">
              {status ?? "Ожидаем запуск"}
            </pre>
          </div>
          <div className="trace-block">
            <h4 className="trace-title">Результат</h4>
            <pre className="output" aria-live="polite">
              {typeof result === "string" ? result : JSON.stringify(result, null, 2)}
            </pre>
          </div>
          <div className="trace-block">
            <h4 className="trace-title">Трасса</h4>
            <pre className="output" aria-live="polite">
              {formatTrace(trace)}
            </pre>
          </div>
        </div>
      </article>
      <article className="panel">
        <header>
          <h3>Редактор программ Δ-VM</h3>
          <p>Редактируйте байткод, запускайте пошаговую трассировку и наблюдайте события в реальном времени.</p>
        </header>
        <form className="form-grid" onSubmit={handleStream}>
          <label htmlFor="program-editor-source">Исходный байткод</label>
          <textarea
            id="program-editor-source"
            value={editorSource}
            onChange={(event) => setEditorSource(event.target.value)}
            disabled={isStreaming}
          />
          <label htmlFor="program-editor-gas">Лимит газа</label>
          <input
            id="program-editor-gas"
            value={editorGasLimit}
            onChange={(event) => setEditorGasLimit(event.target.value)}
            placeholder="Например: 1024"
            inputMode="numeric"
            disabled={isStreaming}
          />
          <div className="button-row">
            <button type="submit" disabled={isStreaming}>
              {isStreaming ? <Spinner /> : "Запустить стрим"}
            </button>
            <button type="button" className="secondary" onClick={stopStreaming} disabled={!isStreaming && !streamSessionId}>
              Остановить
            </button>
          </div>
        </form>
        {streamSessionId ? (
          <p className="muted">Текущая сессия: {streamSessionId}</p>
        ) : (
          <p className="muted">Сессия не активна.</p>
        )}
        {streamError ? <p className="error-text">{streamError}</p> : null}
        <div className="trace-block">
          <h4 className="trace-title">События трассы</h4>
          {streamEvents.length === 0 ? (
            <div className="empty-state">События пока не получены.</div>
          ) : (
            <ul className="history-list">
              {streamEvents.map((eventPayload, index) => (
                <li key={`${eventPayload.type}-${index}`} className="history-item">
                  <header>
                    <span>{eventPayload.type}</span>
                    {eventPayload.timestamp ? <time dateTime={eventPayload.timestamp}>{new Date(eventPayload.timestamp).toLocaleTimeString("ru-RU")}</time> : null}
                  </header>
                  {typeof eventPayload.step === "number" ? <p>Шаг: {eventPayload.step}</p> : null}
                  <pre className="output">{formatTrace(eventPayload.payload)}</pre>
                </li>
              ))}
            </ul>
          )}
        </div>
      </article>
    </section>
  );
}
