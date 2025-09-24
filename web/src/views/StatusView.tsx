// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useEffect, useState } from "react";
import { apiClient, HealthResponse, MetricsResponse } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

function formatDuration(seconds: number | undefined) {
  if (!seconds && seconds !== 0) {
    return "—";
  }
  const total = Math.floor(seconds);
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const secs = total % 60;
  return [hours, minutes, secs]
    .map((unit) => unit.toString().padStart(2, "0"))
    .join(":");
}

function formatMemory(used?: number, total?: number) {
  if (!used && used !== 0) {
    return "—";
  }
  const format = (value: number) => `${(value / (1024 * 1024)).toFixed(1)} МБ`;
  return total ? `${format(used)} / ${format(total)}` : format(used);
}

export function StatusView() {
  const { notify } = useNotifications();
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);

  const refresh = async () => {
    setIsLoading(true);
    try {
      const [healthResponse, metricsResponse] = await Promise.all([apiClient.health(), apiClient.metrics()]);
      setHealth(healthResponse);
      setMetrics(metricsResponse);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить статус", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    void refresh();
  }, []);

  return (
    <section className="view" aria-labelledby="status-tab">
      <article className="panel">
        <header className="inline-actions">
          <div>
            <h2>Состояние узла</h2>
            <p>Оперативные метрики ядра Kolibri Ω.</p>
          </div>
          <button type="button" className="inline" onClick={refresh} disabled={isLoading}>
            {isLoading ? <Spinner /> : "Обновить"}
          </button>
        </header>
        {health ? (
          <div className="metrics-grid">
            <div className="metric-card">
              <span>Uptime</span>
              <strong>{formatDuration(health.uptime)}</strong>
            </div>
            <div className="metric-card">
              <span>Память</span>
              <strong>{formatMemory(health.memory.used, health.memory.total)}</strong>
            </div>
            <div className="metric-card">
              <span>Версия</span>
              <strong>{health.version ?? "—"}</strong>
            </div>
            <div className="metric-card">
              <span>Блоков</span>
              <strong>{health.blocks ?? "—"}</strong>
            </div>
          </div>
        ) : (
          <div className="empty-state">Данные недоступны.</div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Пиры</h3>
          <p>Список соседей и их показатели.</p>
        </header>
        {metrics?.peers && metrics.peers.length ? (
          <ul className="history-list">
            {metrics.peers.map((peer) => (
              <li key={peer.id} className="history-item">
                <header>
                  <span>{peer.id}</span>
                  <span className="badge">{peer.status ?? "unknown"}</span>
                </header>
                {peer.address ? <p>Адрес: {peer.address}</p> : null}
                {typeof peer.latency === "number" ? <p>Задержка: {peer.latency.toFixed(1)} мс</p> : null}
                {typeof peer.score === "number" ? <p>Репутация: {peer.score.toFixed(2)}</p> : null}
              </li>
            ))}
          </ul>
        ) : (
          <div className="empty-state">Нет активных пиров.</div>
        )}
      </article>
    </section>
  );
}
