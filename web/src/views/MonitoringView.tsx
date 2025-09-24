// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useEffect, useMemo, useState } from "react";
import {
  MonitoringAlert,
  MonitoringSnapshot,
  apiClient
} from "../api/client";
import { DataTable } from "../components/DataTable";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

function formatDuration(uptime?: number) {
  if (!uptime || uptime <= 0) {
    return "—";
  }
  const hours = Math.floor(uptime / 3600);
  const minutes = Math.floor((uptime % 3600) / 60);
  return `${hours} ч ${minutes} мин`;
}

function classifySeverity(severity: MonitoringAlert["severity"]) {
  switch (severity) {
    case "critical":
      return "Критический";
    case "warning":
      return "Предупреждение";
    default:
      return "Инфо";
  }
}

export function MonitoringView() {
  const { notify } = useNotifications();
  const [snapshot, setSnapshot] = useState<MonitoringSnapshot | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [isAcknowledging, setIsAcknowledging] = useState<Record<string, boolean>>({});

  const refresh = async () => {
    setIsLoading(true);
    try {
      const data = await apiClient.monitoringSnapshot();
      setSnapshot(data);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить мониторинг", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    void refresh();
  }, []);

  const acknowledgeAlert = async (alertId: string) => {
    setIsAcknowledging((prev) => ({ ...prev, [alertId]: true }));
    try {
      await apiClient.acknowledgeAlert(alertId);
      notify({ title: "Алерт подтверждён", type: "success", timeout: 2000 });
      void refresh();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось подтвердить алерт", message, type: "error" });
    } finally {
      setIsAcknowledging((prev) => ({ ...prev, [alertId]: false }));
    }
  };

  const criticalAlerts = useMemo(() => snapshot?.alerts.filter((alert) => alert.severity === "critical") ?? [], [snapshot?.alerts]);

  return (
    <section className="view" aria-labelledby="monitoring-tab">
      <article className="panel">
        <header className="inline-actions">
          <div>
            <h2>Мониторинг Kolibri Ω</h2>
            <p>Наблюдайте за здоровьем ядра, собирайте метрики и управляйте алертами.</p>
          </div>
          <button type="button" className="inline" onClick={refresh} disabled={isLoading}>
            {isLoading ? <Spinner /> : "Обновить"}
          </button>
        </header>
        <div className="metrics-grid">
          <div className="metric-card">
            <span>Аптайм</span>
            <strong>{formatDuration(snapshot?.health.uptime)}</strong>
          </div>
          <div className="metric-card">
            <span>Память</span>
            <strong>
              {snapshot?.health.memory.used != null && snapshot?.health.memory.total != null
                ? `${snapshot.health.memory.used}/${snapshot.health.memory.total} МБ`
                : "—"}
            </strong>
          </div>
          <div className="metric-card">
            <span>Пиры онлайн</span>
            <strong>{snapshot?.health.peers?.length ?? 0}</strong>
          </div>
          <div className="metric-card">
            <span>Активных задач</span>
            <strong>{snapshot?.metrics.tasksInFlight ?? "—"}</strong>
          </div>
        </div>
      </article>
      <article className="panel">
        <header>
          <h3>Алерты</h3>
          <p>Управляйте событиями PoU/MDL, перегревами и сетевыми аномалиями.</p>
        </header>
        <DataTable
          columns={[
            { key: "title", title: "Событие" },
            {
              key: "severity",
              title: "Уровень",
              render: (value) => classifySeverity(value as MonitoringAlert["severity"])
            },
            {
              key: "raisedAt",
              title: "Поднято",
              render: (value) => (typeof value === "string" ? new Date(value).toLocaleString("ru-RU") : "—")
            },
            {
              key: "actions",
              title: "Действия",
              render: (_value, row) => (
                <button
                  type="button"
                  onClick={() => acknowledgeAlert(row.id)}
                  disabled={Boolean(isAcknowledging[row.id])}
                >
                  {isAcknowledging[row.id] ? <Spinner /> : "Подтвердить"}
                </button>
              )
            }
          ]}
          data={snapshot?.alerts ?? []}
          emptyMessage="Все системы стабильны"
        />
      </article>
      <article className="panel">
        <header>
          <h3>Таймлайн событий</h3>
          <p>Последние события синтеза, блокчейна и планировщика.</p>
        </header>
        {snapshot?.timeline?.length ? (
          <ul className="history-list">
            {snapshot.timeline.map((entry) => (
              <li key={`${entry.timestamp}-${entry.label}`} className="history-item">
                <header>
                  <span>{entry.label}</span>
                  <time dateTime={entry.timestamp}>{new Date(entry.timestamp).toLocaleString("ru-RU")}</time>
                </header>
                {entry.value != null ? <p>Значение: {entry.value}</p> : null}
                {entry.metadata ? <pre className="output">{JSON.stringify(entry.metadata, null, 2)}</pre> : null}
              </li>
            ))}
          </ul>
        ) : (
          <div className="empty-state">Таймлайн пуст.</div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Критические события</h3>
          <p>Отдельный список для срочных PoU/MDL уведомлений.</p>
        </header>
        {criticalAlerts.length ? (
          <ol className="timeline">
            {criticalAlerts.map((alert) => (
              <li key={alert.id}>
                <strong>{alert.title}</strong>
                <span>{new Date(alert.raisedAt).toLocaleTimeString("ru-RU")}</span>
              </li>
            ))}
          </ol>
        ) : (
          <div className="empty-state">Критических алертов нет.</div>
        )}
      </article>
    </section>
  );
}
