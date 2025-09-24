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
