// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import type { JSX } from "react";
import { FormEvent, useMemo, useState } from "react";
import { apiClient, MemoryProgram, MemoryValue } from "../api/client";
import { DataTable } from "../components/DataTable";
import { SkeletonGrid, SkeletonLines } from "../components/Skeleton";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

interface NormalizedProgram extends MemoryProgram {
  displayBytecode?: string;
}

interface PrefixTreeNode {
  id: string;
  label: string;
  count: number;
  children: PrefixTreeNode[];
  depth: number;
}

interface TopProgram {
  id: string;
  score: number;
  description?: string;
}

interface TreeMapNode {
  count: number;
  children: Map<string, TreeMapNode>;
}

function buildPrefixTree(values: MemoryValue[]): PrefixTreeNode[] {
  const rootMap: Map<string, TreeMapNode> = new Map();

  for (const entry of values) {
    const key = entry.key ?? "";
    if (!key) {
      continue;
    }
    let current = rootMap;
    for (let index = 0; index < key.length; index += 1) {
      const digit = key[index];
      const existing = current.get(digit);
      if (existing) {
        existing.count += 1;
        current = existing.children;
      } else {
        const node: TreeMapNode = { count: 1, children: new Map() };
        current.set(digit, node);
        current = node.children;
      }
    }
  }

  const convert = (map: Map<string, TreeMapNode>, prefix: string, depth: number): PrefixTreeNode[] =>
    Array.from(map.entries())
      .map(([digit, node]) => {
        const label = `${prefix}${digit}`;
        return {
          id: label,
          label,
          count: node.count,
          depth,
          children: convert(node.children, label, depth + 1)
        };
      })
      .slice(0, 12);

  return convert(rootMap, "", 0);
}

function highlightPrefix(value: string, prefix: string) {
  if (!prefix || !value.startsWith(prefix)) {
    return value;
  }
  return (
    <span className="value-cell">
      <span className="highlight-prefix">{prefix}</span>
      <span>{value.slice(prefix.length)}</span>
    </span>
  );
}

function renderPrefixNode(node: PrefixTreeNode, prefix: string): JSX.Element {
  return (
    <li key={node.id}>
      <div className="prefix-node">
        <div className="prefix-node__header">
          <span>{highlightPrefix(node.label, prefix)}</span>
          <span>{node.count}</span>
        </div>
        {node.children.length ? (
          <ul className="prefix-children" aria-label={`Дочерние узлы ${node.label}`}>
            {node.children.map((child) => renderPrefixNode(child, prefix))}
          </ul>
        ) : null}
      </div>
    </li>
  );
}

function calculateTopPrograms(programs: MemoryProgram[]): { items: TopProgram[]; maxScore: number } {
  const sorted = programs
    .filter((program): program is MemoryProgram & { score: number; id: string } => typeof program.score === "number" && !!program.id)
    .sort((a, b) => (b.score ?? 0) - (a.score ?? 0))
    .slice(0, 5);

  const maxScore = sorted.reduce((max, program) => Math.max(max, program.score ?? 0), 0);
  return {
    items: sorted.map((program) => ({ id: program.id, score: program.score ?? 0, description: program.description })),
    maxScore
  };
}

export function MemoryView() {
  const { notify } = useNotifications();
  const [prefix, setPrefix] = useState("2");
  const [isLoading, setIsLoading] = useState(false);
  const [values, setValues] = useState<MemoryValue[]>([]);
  const [programs, setPrograms] = useState<MemoryProgram[]>([]);

  const normalizedPrograms = useMemo<NormalizedProgram[]>(
    () =>
      programs.map((program) => ({
        ...program,
        displayBytecode: program.bytecode?.join(", ") ?? "—"
      })),
    [programs]
  );

  const prefixTree = useMemo(() => buildPrefixTree(values), [values]);
  const topPrograms = useMemo(() => calculateTopPrograms(programs), [programs]);
  const memorySummary = useMemo(() => {
    if (!values.length) {
      return {
        totalValues: 0,
        uniqueKeys: 0,
        avgValueLength: 0,
        topDigits: [] as Array<{ digit: number; count: number }>
      };
    }
    const uniqueKeys = new Set<string>();
    let totalLength = 0;
    const digits = Array.from({ length: 10 }, () => 0);
    for (const item of values) {
      uniqueKeys.add(item.key ?? "");
      const valueString = String(item.value ?? "");
      totalLength += valueString.length;
      for (const char of valueString) {
        if (char >= "0" && char <= "9") {
          digits[Number(char)] += 1;
        }
      }
    }
    const topDigits = digits
      .map((count, digit) => ({ digit, count }))
      .filter((item) => item.count > 0)
      .sort((a, b) => b.count - a.count)
      .slice(0, 3);
    return {
      totalValues: values.length,
      uniqueKeys: uniqueKeys.size,
      avgValueLength: totalLength / values.length,
      topDigits
    };
  }, [values]);

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const trimmed = prefix.trim();
    if (!trimmed) {
      notify({ title: "Введите префикс", message: "Для запроса к памяти необходим цифровой префикс", type: "error" });
      return;
    }

    setIsLoading(true);
    try {
      const response = await apiClient.getMemory(trimmed);
      setValues(
        Array.isArray(response.values)
          ? response.values.map((value) =>
              typeof value === "string"
                ? { key: trimmed, value }
                : { key: "key" in value ? value.key : trimmed, value: String((value as MemoryValue).value) }
            )
          : []
      );
      setPrograms(Array.isArray(response.programs) ? response.programs : []);
      notify({ title: "Данные памяти обновлены", type: "success", timeout: 2500 });
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Ошибка чтения памяти", message, type: "error" });
      setValues([]);
      setPrograms([]);
    } finally {
      setIsLoading(false);
    }
  };

  return (
    <section className="view" aria-labelledby="memory-tab">
      <article className="panel">
        <header>
          <h2>F-KV Explorer</h2>
          <p>Исследуйте десятичную память Kolibri Ω по префиксам и просматривайте связанные программы.</p>
        </header>
        <form className="form-grid" onSubmit={handleSubmit}>
          <label htmlFor="memory-prefix">Цифровой префикс</label>
          <input
            id="memory-prefix"
            value={prefix}
            onChange={(event) => setPrefix(event.target.value)}
            placeholder="Например: 2019"
            inputMode="numeric"
            disabled={isLoading}
          />
          <button type="submit" disabled={isLoading}>
            {isLoading ? <Spinner /> : "Загрузить"}
          </button>
        </form>
      </article>
      <article className="panel">
        <header>
          <h3>Значения по префиксу</h3>
          <p>Возвращаем до 20 релевантных записей F-KV.</p>
        </header>
        {isLoading ? (
          <SkeletonLines count={4} />
        ) : (
          <DataTable
            columns={[
              {
                key: "key",
                title: "Префикс",
                render: (value: MemoryValue["key"]) => highlightPrefix(String(value), prefix)
              },
              { key: "value", title: "Значение" }
            ]}
            data={values}
            emptyMessage="Значения не найдены"
          />
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Связанные программы</h3>
          <p>Релевантные байткоды и их метрики.</p>
        </header>
        {isLoading ? (
          <SkeletonGrid columns={3} rows={2} />
        ) : (
          <DataTable
            columns={[
              { key: "id", title: "ID" },
              { key: "score", title: "Score" },
              { key: "description", title: "Описание" },
              { key: "displayBytecode", title: "Байткод" }
            ]}
            data={normalizedPrograms}
            emptyMessage="Программы не найдены"
          />
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Сводка выборки</h3>
          <p>Обзор характеристик последнего чтения памяти.</p>
        </header>
        {memorySummary.totalValues === 0 ? (
          <div className="empty-state">Загрузите данные, чтобы увидеть статистику.</div>
        ) : (
          <div className="memory-summary">
            <div className="memory-summary__metrics">
              <div className="metric-card">
                <span>Записей</span>
                <strong>{memorySummary.totalValues}</strong>
              </div>
              <div className="metric-card">
                <span>Уникальных ключей</span>
                <strong>{memorySummary.uniqueKeys}</strong>
              </div>
              <div className="metric-card">
                <span>Средняя длина</span>
                <strong>{memorySummary.avgValueLength.toFixed(1)}</strong>
              </div>
            </div>
            <div>
              <h4 className="trace-title">Топ цифр</h4>
              {memorySummary.topDigits.length === 0 ? (
                <p className="memory-summary__empty">В значениях нет цифр.</p>
              ) : (
                <ul className="memory-summary__digits">
                  {memorySummary.topDigits.map((item) => (
                    <li key={item.digit}>
                      <span className="digit-analytics__digit">{item.digit}</span>
                      <span className="digit-analytics__count">{item.count}</span>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          </div>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Дерево префиксов</h3>
          <p>Иерархия значений по ключам памяти. Ограничиваем визуализацию двенадцатью узлами на уровень.</p>
        </header>
        {prefixTree.length === 0 ? (
          <div className="empty-state">Нет данных для построения дерева.</div>
        ) : (
          <ul className="prefix-tree">
            {prefixTree.map((node) => renderPrefixNode(node, prefix))}
          </ul>
        )}
      </article>
      <article className="panel">
        <header>
          <h3>Top-K по score</h3>
          <p>Сравнение лучших программ по метрике score.</p>
        </header>
        {topPrograms.items.length === 0 ? (
          <div className="empty-state">Нет программ с оценкой score.</div>
        ) : (
          <div className="topk-chart">
            {topPrograms.items.map((program) => {
              const widthPercent = topPrograms.maxScore > 0 ? Math.round((program.score / topPrograms.maxScore) * 100) : 0;
              return (
                <div key={program.id} className="topk-row">
                  <div className="topk-row__meta">
                    <span>{program.id}</span>
                    <span>{program.score.toFixed(2)}</span>
                  </div>
                  <div className="topk-row__bar" style={{ width: `${widthPercent}%` }} />
                  {program.description ? <span className="topk-row__description">{program.description}</span> : null}
                </div>
              );
            })}
          </div>
        )}
      </article>
    </section>
  );
}
