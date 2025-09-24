// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useEffect, useMemo, useState } from "react";
import { apiClient, MetricsResponse, PeerCommandRequest, PeerInfo } from "../api/client";
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
  const latencies = online
    .map((peer) => (typeof peer.latency === "number" ? peer.latency : null))
    .filter((value): value is number => value != null);
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
  const [selectedPeer, setSelectedPeer] = useState<PeerInfo | null>(null);
  const [scoreInput, setScoreInput] = useState("");
  const [roleInput, setRoleInput] = useState("");
  const [quarantine, setQuarantine] = useState(false);
  const [isUpdating, setIsUpdating] = useState(false);

  const summary = useMemo(() => computeSummary(metrics?.peers), [metrics?.peers]);

  const refresh = async () => {
    setIsLoading(true);
    try {
      const data = await apiClient.metrics();
      setMetrics(data);
      if (selectedPeer) {
        const updated = data.peers?.find((peer) => peer.id === selectedPeer.id);
        if (updated) {
          setSelectedPeer(updated);
          setScoreInput(updated.score != null ? updated.score.toString() : "");
          setRoleInput(updated.role ?? "");
        }
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить состояние кластера", message, type: "error" });
    } finally {
      setIsLoading(false);
    }
  };

  useEffect(() => {
    void refresh();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const handleSelectPeer = (peer: PeerInfo) => {
    setSelectedPeer(peer);
    setScoreInput(peer.score != null ? peer.score.toString() : "");
    setRoleInput(peer.role ?? "");
    setQuarantine(peer.status === "quarantine");
  };

  const handlePeerUpdate = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!selectedPeer) {
      return;
    }
    setIsUpdating(true);
    try {
      const payload: PeerCommandRequest = {
        role: roleInput.trim() || undefined,
        quarantine,
        score: scoreInput.trim() ? Number(scoreInput) : undefined
      };
      if (payload.score != null && !Number.isFinite(payload.score)) {
        throw new Error("Скора должен быть числом");
      }
      const response = await apiClient.updatePeer(selectedPeer.id, payload);
      if (!response.acknowledged) {
        notify({ title: "Команда не подтверждена", message: response.message, type: "warning" });
      } else {
        notify({ title: "Параметры peer обновлены", type: "success", timeout: 2000 });
        await refresh();
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить peer", message, type: "error" });
    } finally {
      setIsUpdating(false);
    }
  };

  const handleDisconnect = async () => {
    if (!selectedPeer) {
      return;
    }
    setIsUpdating(true);
    try {
      await apiClient.disconnectPeer(selectedPeer.id);
      notify({ title: "Peer отключён", type: "info", timeout: 2000 });
      setSelectedPeer(null);
      setScoreInput("");
      setRoleInput("");
      setQuarantine(false);
      await refresh();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось отключить peer", message, type: "error" });
    } finally {
      setIsUpdating(false);
    }
  };

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
            <strong>{summary.averageLatency != null ? `${summary.averageLatency.toFixed(1)} мс` : "—"}</strong>
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
            { key: "role", title: "Роль" },
            {
              key: "actions",
              title: "Управление",
              render: (_value, row) => (
                <button type="button" onClick={() => handleSelectPeer(row)}>
                  Настроить
                </button>
              )
            }
          ]}
          data={metrics?.peers ?? []}
          emptyMessage="Пиры не обнаружены"
        />
      </article>
      <article className="panel">
        <header>
          <h3>Менеджер кластера</h3>
          <p>Настройка ролей, репутаций и карантина для выбранного узла.</p>
        </header>
        {selectedPeer ? (
          <form className="form-grid" onSubmit={handlePeerUpdate}>
            <div className="peer-summary">
              <strong>{selectedPeer.id}</strong>
              <span>{selectedPeer.address ?? "—"}</span>
            </div>
            <label htmlFor="peer-score">Новая репутация</label>
            <input
              id="peer-score"
              value={scoreInput}
              onChange={(event) => setScoreInput(event.target.value)}
              inputMode="decimal"
              disabled={isUpdating}
            />
            <label htmlFor="peer-role">Роль</label>
            <input
              id="peer-role"
              value={roleInput}
              onChange={(event) => setRoleInput(event.target.value)}
              disabled={isUpdating}
            />
            <label className="checkbox">
              <input
                type="checkbox"
                checked={quarantine}
                onChange={(event) => setQuarantine(event.target.checked)}
                disabled={isUpdating}
              />
              Поместить в карантин
            </label>
            <div className="button-row">
              <button type="submit" disabled={isUpdating}>
                {isUpdating ? <Spinner /> : "Сохранить"}
              </button>
              <button type="button" onClick={handleDisconnect} disabled={isUpdating}>
                Отключить peer
              </button>
            </div>
          </form>
        ) : (
          <div className="empty-state">Выберите peer для управления.</div>
        )}
      </article>
    </section>
  );
}
