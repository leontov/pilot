// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useMemo, useState } from "react";

interface TimelineItem {
  step: number;
  instruction?: string;
  gas?: number;
  stack?: string[];
  registers?: Record<string, unknown>;
  raw: unknown;
}

interface TraceViewerProps {
  trace: unknown;
  title?: string;
}

function normalizeValue(value: unknown): string {
  if (Array.isArray(value)) {
    return value.map(normalizeValue).join(", ");
  }
  if (typeof value === "object" && value != null) {
    return JSON.stringify(value);
  }
  return String(value);
}

function stringifyRaw(value: unknown) {
  try {
    return JSON.stringify(value, null, 2);
  } catch (error) {
    return String(value);
  }
}

function toTimeline(trace: unknown): TimelineItem[] | null {
  if (!Array.isArray(trace)) {
    return null;
  }

  const entries: TimelineItem[] = [];
  for (let index = 0; index < trace.length; index += 1) {
    const item = trace[index];
    if (!item || typeof item !== "object") {
      return null;
    }
    const record = item as Record<string, unknown>;
    const stackValue = record.stack;
    const registersValue = record.registers;

    entries.push({
      step: typeof record.step === "number" ? record.step : index,
      instruction: typeof record.instruction === "string" ? record.instruction : typeof record.op === "string" ? record.op : undefined,
      gas: typeof record.gasUsed === "number" ? record.gasUsed : typeof record.gas === "number" ? record.gas : undefined,
      stack: Array.isArray(stackValue) ? stackValue.map(normalizeValue) : undefined,
      registers: typeof registersValue === "object" && registersValue != null ? (registersValue as Record<string, unknown>) : undefined,
      raw: item
    });
  }

  return entries;
}

export function TraceViewer({ trace, title }: TraceViewerProps) {
  const timeline = useMemo(() => toTimeline(trace), [trace]);
  const [mode, setMode] = useState<"timeline" | "raw">(timeline ? "timeline" : "raw");

  if (trace == null) {
    return <div className="empty-state">Нет трассировки</div>;
  }

  const rawContent = stringifyRaw(trace);
  const canUseTimeline = timeline && timeline.length > 0;

  return (
    <div className="trace-viewer">
      <header className="trace-viewer__header">
        <div>
          <h4>{title ?? "Трасса исполнения"}</h4>
          <p>{mode === "timeline" ? "Пошаговая визуализация Δ-VM" : "Исходный JSON трассы"}</p>
        </div>
        {canUseTimeline ? (
          <div className="segmented-control" role="radiogroup" aria-label="Режим отображения трассы">
            <button
              type="button"
              role="radio"
              aria-checked={mode === "timeline"}
              className={mode === "timeline" ? "active" : ""}
              onClick={() => setMode("timeline")}
            >
              Таймлайн
            </button>
            <button
              type="button"
              role="radio"
              aria-checked={mode === "raw"}
              className={mode === "raw" ? "active" : ""}
              onClick={() => setMode("raw")}
            >
              JSON
            </button>
          </div>
        ) : null}
      </header>
      {mode === "timeline" && canUseTimeline ? (
        <ol className="trace-timeline">
          {timeline.map((item) => (
            <li key={item.step} className="trace-timeline__item">
              <div className="trace-timeline__meta">
                <span className="trace-step">Шаг {item.step}</span>
                {item.instruction ? <span className="trace-op">{item.instruction}</span> : null}
                {typeof item.gas === "number" ? <span className="trace-gas">Gas: {item.gas}</span> : null}
              </div>
              {item.stack && item.stack.length ? (
                <div className="trace-section">
                  <span className="trace-section__title">Стек</span>
                  <span className="trace-section__value">{item.stack.join(" → ")}</span>
                </div>
              ) : null}
              {item.registers && Object.keys(item.registers).length ? (
                <div className="trace-section">
                  <span className="trace-section__title">Регистры</span>
                  <span className="trace-section__value">
                    {Object.entries(item.registers)
                      .map(([key, value]) => `${key}: ${normalizeValue(value)}`)
                      .join(" · ")}
                  </span>
                </div>
              ) : null}
              <details className="trace-details">
                <summary>Подробности шага</summary>
                <pre>{stringifyRaw(item.raw)}</pre>
              </details>
            </li>
          ))}
        </ol>
      ) : (
        <pre className="output">{rawContent}</pre>
      )}
    </div>
  );
}
