// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useMemo, useState } from "react";
import { apiClient } from "../api/client";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

interface SubmissionEntry {
  id: string;
  request: { bytecode: number[]; notes?: string };
  response: {
    poe?: number;
    mdl?: number;
    score?: number;
    accepted?: boolean;
    programId?: string;
  };
}

function parseBytecode(raw: string): number[] {
  const parts = raw
    .split(/[^0-9-]+/)
    .map((part) => part.trim())
    .filter(Boolean);
  if (!parts.length) {
    throw new Error("Введите байткод");
  }
  return parts.map((part) => {
    const value = Number(part);
    if (!Number.isFinite(value) || !Number.isInteger(value)) {
      throw new Error(`Некорректное значение: ${part}`);
    }
    return value;
  });
}

export function SynthView() {
  const { notify } = useNotifications();
  const [bytecode, setBytecode] = useState("16,0,0,2");
  const [notes, setNotes] = useState("Тестовая программа");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [submissions, setSubmissions] = useState<SubmissionEntry[]>([]);
  const summary = useMemo(() => {
    if (!submissions.length) {
      return { total: 0 } as const;
    }
    let bestScore = -Infinity;
    let bestProgramId: string | undefined;
    let poeSum = 0;
    let poeCount = 0;
    let mdlSum = 0;
    let mdlCount = 0;
    let accepted = 0;
    for (const submission of submissions) {
      const { score, poe, mdl, accepted: isAccepted, programId } = submission.response;
      if (typeof score === "number" && score > bestScore) {
        bestScore = score;
        bestProgramId = programId ?? submission.id;
      }
      if (typeof poe === "number") {
        poeSum += poe;
        poeCount += 1;
      }
      if (typeof mdl === "number") {
        mdlSum += mdl;
        mdlCount += 1;
      }
      if (isAccepted) {
        accepted += 1;
      }
    }
    return {
      total: submissions.length,
      bestScore: Number.isFinite(bestScore) ? bestScore : undefined,
      bestProgramId,
      averagePoe: poeCount ? poeSum / poeCount : undefined,
      averageMdl: mdlCount ? mdlSum / mdlCount : undefined,
      acceptanceRate: submissions.length ? (accepted / submissions.length) * 100 : undefined
    } as const;
  }, [submissions]);

  const chart = useMemo(() => {
    if (!submissions.length) {
      return { hasData: false, width: 320, height: 160, poePolyline: "", mdlPolyline: "" } as const;
    }
    const history = [...submissions].reverse();
    const width = 320;
    const height = 160;
    const pad = 18;

    const buildPolyline = (values: (number | undefined)[], invert: boolean) => {
      const filtered = values.filter((value): value is number => typeof value === "number");
      if (!filtered.length) {
        return "";
      }
      const min = Math.min(...filtered);
      const max = Math.max(...filtered);
      const span = max - min || 1;
      const step = values.length > 1 ? (width - 2 * pad) / (values.length - 1) : 0;
      return values
        .map((value, index) => {
          if (typeof value !== "number") {
            return null;
          }
          const x = pad + step * index;
          const ratio = (value - min) / span;
          const y = invert
            ? pad + (height - 2 * pad) * ratio
            : height - pad - (height - 2 * pad) * ratio;
          return `${x.toFixed(1)},${y.toFixed(1)}`;
        })
        .filter(Boolean)
        .join(" ");
    };

    const poeValues = history.map((entry) => (typeof entry.response.poe === "number" ? entry.response.poe : undefined));
    const mdlValues = history.map((entry) => (typeof entry.response.mdl === "number" ? entry.response.mdl : undefined));

    const poePolyline = buildPolyline(poeValues, false);
    const mdlPolyline = buildPolyline(mdlValues, true);

    return {
      hasData: Boolean(poePolyline || mdlPolyline),
      width,
      height,
      poePolyline,
      mdlPolyline
    } as const;
  }, [submissions]);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    let program: number[];
    try {
      program = parseBytecode(bytecode);
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка ввода", message, type: "error" });
      return;
    }

    setIsSubmitting(true);
    try {
      const response = await apiClient.submitProgram({ bytecode: program, notes: notes.trim() || undefined });
      const entry: SubmissionEntry = {
        id: response.programId ?? `${Date.now()}`,
        request: { bytecode: program, notes: notes.trim() || undefined },
        response
      };
      setSubmissions((prev) => [entry, ...prev].slice(0, 10));
      notify({ title: "Кандидат отправлен", message: "Ответ синтезатора получен", type: "success", timeout: 2500 });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка синтеза", message, type: "error" });
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <section className="view" aria-labelledby="synth-tab">
      <article className="panel">
        <header>
          <h2>Оценка программ</h2>
          <p>Отправьте байткод кандидата для расчёта PoE, MDL и итогового score.</p>
        </header>
        <form className="form-grid" onSubmit={handleSubmit}>
          <label htmlFor="synth-bytecode">Байткод</label>
          <textarea
            id="synth-bytecode"
            value={bytecode}
            onChange={(event) => setBytecode(event.target.value)}
            disabled={isSubmitting}
          />
          <label htmlFor="synth-notes">Комментарий (опционально)</label>
          <input
            id="synth-notes"
            value={notes}
            onChange={(event) => setNotes(event.target.value)}
            placeholder="Например: поиск арифметики"
            disabled={isSubmitting}
          />
          <button type="submit" disabled={isSubmitting}>
            {isSubmitting ? <Spinner /> : "Отправить"}
          </button>
        </form>
      </article>
      <article className="panel">
        <header>
          <h3>Сводка оценок</h3>
          <p>Агрегированные показатели по последним кандидатам.</p>
        </header>
        {summary.total === 0 ? (
          <div className="empty-state">Отправьте программу, чтобы увидеть метрики.</div>
        ) : (
          <div className="metrics-grid">
            <div className="metric-card">
              <span>Всего кандидатов</span>
              <strong>{summary.total}</strong>
            </div>
            <div className="metric-card">
              <span>Лучший score</span>
              <strong>{summary.bestScore != null ? summary.bestScore.toFixed(2) : "—"}</strong>
              {summary.bestProgramId ? <small>#{summary.bestProgramId}</small> : null}
            </div>
            <div className="metric-card">
              <span>Средний PoE</span>
              <strong>{summary.averagePoe != null ? summary.averagePoe.toFixed(3) : "—"}</strong>
            </div>
            <div className="metric-card">
              <span>Средний MDL</span>
              <strong>{summary.averageMdl != null ? summary.averageMdl.toFixed(3) : "—"}</strong>
            </div>
            <div className="metric-card">
              <span>Принято</span>
              <strong>
                {summary.acceptanceRate != null ? `${summary.acceptanceRate.toFixed(1)}%` : "—"}
              </strong>
            </div>
          </div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Динамика PoE/MDL</h3>
          <p>Графики помогают отследить тренды качества программ.</p>
        </header>
        {chart.hasData ? (
          <div className="chart" style={{ display: "flex", flexDirection: "column", gap: "0.75rem" }}>
            <svg
              viewBox={`0 0 ${chart.width} ${chart.height}`}
              role="img"
              aria-label="История метрик PoE и MDL"
              style={{ width: "100%", maxWidth: "360px", height: "auto" }}
            >
              <rect x={0.5} y={0.5} width={chart.width - 1} height={chart.height - 1} fill="none" stroke="#ccc" strokeWidth={1} />
              {chart.mdlPolyline ? (
                <polyline points={chart.mdlPolyline} fill="none" stroke="#c92a2a" strokeWidth={2} strokeLinecap="round" />
              ) : null}
              {chart.poePolyline ? (
                <polyline points={chart.poePolyline} fill="none" stroke="#2b8a3e" strokeWidth={2} strokeLinecap="round" />
              ) : null}
            </svg>
            <div style={{ display: "flex", gap: "1.5rem", fontSize: "0.875rem" }}>
              <span style={{ display: "inline-flex", alignItems: "center", gap: "0.5rem" }}>
                <span style={{ width: "0.75rem", height: "0.75rem", backgroundColor: "#2b8a3e", borderRadius: "9999px" }} />
                PoE
              </span>
              <span style={{ display: "inline-flex", alignItems: "center", gap: "0.5rem" }}>
                <span style={{ width: "0.75rem", height: "0.75rem", backgroundColor: "#c92a2a", borderRadius: "9999px" }} />
                MDL
              </span>
            </div>
          </div>
        ) : (
          <div className="empty-state">Недостаточно данных для построения графиков.</div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Последние отправки</h3>
          <p>Отслеживайте как изменяются метрики PoE/MDL по кандидатам.</p>
        </header>
        {submissions.length === 0 ? (
          <div className="empty-state">Пока нет отправленных программ.</div>
        ) : (
          <ul className="history-list">
            {submissions.map((entry) => (
              <li key={entry.id} className="history-item">
                <header>
                  <span>{entry.response.programId ?? entry.id}</span>
                  <span className="badge">Score: {entry.response.score ?? "—"}</span>
                </header>
                <p>PoE: {entry.response.poe ?? "—"} · MDL: {entry.response.mdl ?? "—"}</p>
                <p>Байткод: {entry.request.bytecode.join(", ")}</p>
                {entry.request.notes ? <p>Комментарий: {entry.request.notes}</p> : null}
              </li>
            ))}
          </ul>
        )}
      </article>
    </section>
  );
}
