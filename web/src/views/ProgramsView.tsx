// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useState } from "react";
import { apiClient } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";
import { TraceViewer } from "../components/TraceViewer";

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

export function ProgramsView() {
  const { notify } = useNotifications();
  const [programSource, setProgramSource] = useState("16,0,0,2");
  const [gasLimit, setGasLimit] = useState("256");
  const [isLoading, setIsLoading] = useState(false);
  const [status, setStatus] = useState<string | null>(null);
  const [result, setResult] = useState<unknown>(null);
  const [trace, setTrace] = useState<unknown>(null);

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
            <TraceViewer trace={trace} title="Трасса" />
          </div>
        </div>
      </article>
    </section>
  );
}
