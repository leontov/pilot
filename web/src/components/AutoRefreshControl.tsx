// Copyright (c) 2024 Кочуров Владислав Евгеньевич

interface AutoRefreshControlProps {
  enabled: boolean;
  onToggle: (next: boolean) => void;
  lastUpdated: Date | null;
  intervalMs?: number;
  isLoading?: boolean;
}

function formatTimestamp(timestamp: Date | null) {
  if (!timestamp) {
    return "—";
  }
  return timestamp.toLocaleTimeString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  });
}

export function AutoRefreshControl({ enabled, onToggle, lastUpdated, intervalMs, isLoading }: AutoRefreshControlProps) {
  const intervalSeconds = intervalMs && intervalMs > 0 ? Math.round(intervalMs / 1000) : null;

  return (
    <div className="auto-refresh-control" role="group" aria-label="Настройки автообновления">
      <label className="checkbox">
        <input
          type="checkbox"
          checked={enabled}
          onChange={(event) => onToggle(event.target.checked)}
          aria-label="Включить автообновление"
        />
        <span>
          Автообновление
          {intervalSeconds ? ` (${intervalSeconds} с)` : ""}
        </span>
      </label>
      <span className="muted" aria-live="polite">
        Обновлено: {formatTimestamp(lastUpdated)}
        {isLoading ? " · синхронизация…" : ""}
      </span>
    </div>
  );
}
