// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useCallback, useEffect, useState } from "react";
import { apiClient, ChainSubmitResponse, MetricsResponse } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";
import { ToggleSwitch } from "../components/ToggleSwitch";
import { usePersistentState } from "../hooks/usePersistentState";
import { useAutoRefresh } from "../hooks/useAutoRefresh";

interface ChainEntry {
  timestamp: string;
  response: ChainSubmitResponse;
  programId: string;
}

function formatTimestamp(value: Date | string) {
  const date = value instanceof Date ? value : new Date(value);
  return date.toLocaleString("ru-RU", { hour: "2-digit", minute: "2-digit", second: "2-digit", day: "2-digit", month: "2-digit" });
}

export function ChainView() {
  const { notify } = useNotifications();
  const [programId, setProgramId] = useState("");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [history, setHistory] = useState<ChainEntry[]>([]);
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null);
  const [isLoadingMetrics, setIsLoadingMetrics] = useState(false);
  const [autoRefreshMetrics, setAutoRefreshMetrics] = usePersistentState("kolibri-chain-auto-refresh", true);

  const submitChain = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!programId.trim()) {
      notify({ title: "Введите ID программы", type: "error" });
      return;
    }

    setIsSubmitting(true);
    try {
      const response = await apiClient.submitChain({ programId: programId.trim() });
      const entry: ChainEntry = {
        timestamp: formatTimestamp(new Date()),
        response,
        programId: programId.trim()
      };
      setHistory((prev) => [entry, ...prev].slice(0, 10));
      notify({ title: "Кандидат отправлен в цепочку", message: response.status, type: "success", timeout: 2500 });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка цепочки", message, type: "error" });
    } finally {
      setIsSubmitting(false);
    }
  };

  const refreshMetrics = useCallback(async () => {
    setIsLoadingMetrics(true);
    try {
      const data = await apiClient.metrics();
      setMetrics(data);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось загрузить метрики цепочки", message, type: "error" });
    } finally {
      setIsLoadingMetrics(false);
    }
  }, [notify]);

  useEffect(() => {
    void refreshMetrics();
  }, [refreshMetrics]);

  useAutoRefresh(refreshMetrics, autoRefreshMetrics, { intervalMs: 20000, runImmediately: true });

  return (
    <section className="view" aria-labelledby="chain-tab">
      <article className="panel">
        <header>
          <h2>Блокчейн знаний</h2>
          <p>Отправляйте проверенные программы в сеть и отслеживайте состояние цепочки.</p>
        </header>
        <form className="form-grid" onSubmit={submitChain}>
          <label htmlFor="chain-program">ID программы</label>
          <input
            id="chain-program"
            value={programId}
            onChange={(event) => setProgramId(event.target.value)}
            placeholder="Например: prog-001"
            disabled={isSubmitting}
          />
          <button type="submit" disabled={isSubmitting}>
            {isSubmitting ? <Spinner /> : "Отправить в цепочку"}
          </button>
        </form>
      </article>
      <article className="panel">
        <header className="inline-actions">
          <div>
            <h3>Метрики цепочки</h3>
            <p>Кол-во блоков, активные задачи и время последнего блока.</p>
          </div>
          <div className="inline-actions">
            <ToggleSwitch
              id="chain-auto-refresh"
              checked={autoRefreshMetrics}
              onChange={setAutoRefreshMetrics}
              label="Автообновление"
              description="20 секунд"
            />
            <button type="button" className="inline" onClick={refreshMetrics} disabled={isLoadingMetrics}>
              {isLoadingMetrics ? <Spinner /> : "Обновить"}
            </button>
          </div>
        </header>
        {metrics ? (
          <div className="metrics-grid">
            <div className="metric-card">
              <span>Блоков всего</span>
              <strong>{metrics.blocks ?? "—"}</strong>
            </div>
            <div className="metric-card">
              <span>Задач в обработке</span>
              <strong>{metrics.tasksInFlight ?? "—"}</strong>
            </div>
            <div className="metric-card">
              <span>Последний блок</span>
              <strong>{metrics.lastBlockTime ? formatTimestamp(metrics.lastBlockTime) : "—"}</strong>
            </div>
          </div>
        ) : (
          <div className="empty-state">Метрики пока не загружены.</div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>История отправок</h3>
          <p>Последние запросы в блокчейн Kolibri Ω.</p>
        </header>
        {history.length === 0 ? (
          <div className="empty-state">История пуста.</div>
        ) : (
          <ul className="history-list">
            {history.map((entry) => (
              <li key={`${entry.timestamp}-${entry.programId}`} className="history-item">
                <header>
                  <span>{entry.programId}</span>
                  <time dateTime={entry.timestamp}>{entry.timestamp}</time>
                </header>
                <p>Статус: {entry.response.status}</p>
                {entry.response.blockId ? <p>Блок: {entry.response.blockId}</p> : null}
                {typeof entry.response.position === "number" ? <p>Позиция: {entry.response.position}</p> : null}
              </li>
            ))}
          </ul>
        )}
      </article>
    </section>
  );
}
