// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useEffect, useMemo, useState } from "react";
import { apiClient, MetricsResponse, PeerInfo } from "../api/client";
import { DataTable } from "../components/DataTable";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

interface ClusterSummary {
  peersOnline: number;
  averageLatency?: number;
  bestPeer?: PeerInfo;
}

function computeSummary(peers: PeerInfo[] | undefined): ClusterSummary {
  if (!peers || peers.length === 0) {
    return { peersOnline: 0 };
  }
  const online = peers.filter((peer) => peer.status !== "offline");
  const latencies = online.map((peer) => (typeof peer.latency === "number" ? peer.latency : null)).filter((value): value is number => value != null);
  const averageLatency = latencies.length ? latencies.reduce((sum, value) => sum + value, 0) / latencies.length : undefined;
  const bestPeer = online.reduce<PeerInfo | undefined>((best, peer) => {
    if (!best) return peer;
    const bestScore = typeof best.score === "number" ? best.score : -Infinity;
    const peerScore = typeof peer.score === "number" ? peer.score : -Infinity;
    return peerScore > bestScore ? peer : best;
  }, undefined);
  return { peersOnline: online.length, averageLatency, bestPeer };
}

export function ClusterView() {
  const { notify } = useNotifications();
  const [metrics, setMetrics] = useState<MetricsResponse | null>(null);
  const [isLoading, setIsLoading] = useState(false);

  const summary = useMemo(() => computeSummary(metrics?.peers), [metrics?.peers]);

  const refresh = async () => {
    setIsLoading(true);
    try {
      const data = await apiClient.metrics();
      setMetrics(data);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить состояние кластера", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    void refresh();
  }, []);

  return (
    <section className="view" aria-labelledby="cluster-tab">
      <article className="panel">
        <header className="inline-actions">
          <div>
            <h2>Роевой разум</h2>
            <p>Сводка по соседям и сетевому состоянию.</p>
          </div>
          <button type="button" className="inline" onClick={refresh} disabled={isLoading}>
            {isLoading ? <Spinner /> : "Обновить"}
          </button>
        </header>
        <div className="metrics-grid">
          <div className="metric-card">
            <span>Активных пиров</span>
            <strong>{summary.peersOnline}</strong>
          </div>
          <div className="metric-card">
            <span>Средняя задержка</span>
            <strong>
              {summary.averageLatency != null ? `${summary.averageLatency.toFixed(1)} мс` : "—"}
            </strong>
          </div>
          <div className="metric-card">
            <span>Лучший peer</span>
            <strong>{summary.bestPeer?.id ?? "—"}</strong>
          </div>
        </div>
      </article>
      <article className="panel">
        <header>
          <h3>Топология кластера</h3>
          <p>Подробности по каждому узлу.</p>
        </header>
        <DataTable
          columns={[
            { key: "id", title: "ID" },
            { key: "status", title: "Статус" },
            {
              key: "latency",
              title: "Задержка",
              render: (value) => (typeof value === "number" ? `${value.toFixed(1)} мс` : "—")
            },
            {
              key: "score",
              title: "Репутация",
              render: (value) => (typeof value === "number" ? value.toFixed(2) : "—")
            },
            { key: "address", title: "Адрес" },
            { key: "role", title: "Роль" }
          ]}
          data={metrics?.peers ?? []}
          emptyMessage="Пиры не обнаружены"
        />
      </article>
    </section>
  );
}
