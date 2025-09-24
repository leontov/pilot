// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { FormEvent, useCallback, useMemo, useState } from "react";
import {
  apiClient,
  ScheduledTask,
  TaskActionResponse,
  TaskCreateRequest,
  TaskUpdateRequest
} from "../api/client";
import { DataTable } from "../components/DataTable";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";
import { AutoRefreshControl } from "../components/AutoRefreshControl";
import { useLiveQuery } from "../hooks/useLiveQuery";

interface EditableTask extends ScheduledTask {
  effectivePriority: number;
}

function safeParsePayload(payload: string): unknown {
  if (!payload.trim()) {
    return undefined;
  }
  try {
    return JSON.parse(payload);
  } catch (error) {
    throw new Error("Payload должен быть корректным JSON");
  }
}

function mapTasks(tasks: ScheduledTask[]): EditableTask[] {
  return tasks.map((task) => ({
    ...task,
    effectivePriority: typeof task.priority === "number" ? task.priority : 0
  }));
}

function formatTimestamp(timestamp?: string) {
  if (!timestamp) {
    return "—";
  }
  return new Date(timestamp).toLocaleString("ru-RU", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    day: "2-digit",
    month: "2-digit"
  });
}

export function SchedulerView() {
  const { notify } = useNotifications();
  const [autoRefresh, setAutoRefresh] = useState(true);
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [name, setName] = useState("vm.run");
  const [priority, setPriority] = useState("5");
  const [schedule, setSchedule] = useState("immediate");
  const [payload, setPayload] = useState("{\n  \"program\": [16,0,0,2]\n}");

  const handleError = useCallback(
    (error: unknown) => {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось загрузить задачи", message, type: "error" });
    },
    [notify]
  );

  const fetchTasks = useCallback(() => apiClient.listTasks(), []);

  const { data: rawTasks = [], refresh, isLoading, lastUpdated } = useLiveQuery<ScheduledTask[]>({
    fetcher: fetchTasks,
    intervalMs: autoRefresh ? 10000 : 0,
    onError: handleError,
    initialData: []
  });

  const tasks = useMemo(() => mapTasks(rawTasks), [rawTasks]);
  const activeTasks = useMemo(
    () => tasks.filter((task) => task.status === "running" || task.status === "queued"),
    [tasks]
  );

  const handleCreate = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    setIsSubmitting(true);
    try {
      const parsedPayload = safeParsePayload(payload);
      const priorityValue = priority.trim() ? Number(priority) : undefined;
      const request: TaskCreateRequest = {
        name: name.trim(),
        priority: Number.isFinite(priorityValue) ? priorityValue : undefined,
        schedule: schedule.trim() || undefined,
        payload: parsedPayload
      };
      const result = await apiClient.createTask(request);
      if (!result.acknowledged) {
        notify({ title: "Планировщик отклонил задачу", message: result.message, type: "warning" });
      } else {
        notify({ title: "Задача создана", type: "success", timeout: 2500 });
        void refresh();
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось создать задачу", message, type: "error" });
    } finally {
      setIsSubmitting(false);
    }
  };

  const updateTask = async (taskId: string, patch: TaskUpdateRequest) => {
    try {
      const response: TaskActionResponse = await apiClient.updateTask(taskId, patch);
      if (!response.acknowledged) {
        notify({ title: "Операция не подтверждена", message: response.message, type: "warning" });
      }
      void refresh();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось обновить задачу", message, type: "error" });
    }
  };

  const cancelTask = async (taskId: string) => {
    try {
      const response = await apiClient.cancelTask(taskId);
      if (!response.acknowledged) {
        notify({ title: "Отмена не подтверждена", message: response.message, type: "warning" });
      }
      void refresh();
    } catch (error) {
      const message = error instanceof Error ? error.message : "Неизвестная ошибка";
      notify({ title: "Не удалось отменить задачу", message, type: "error" });
    }
  };

  return (
    <section className="view" aria-labelledby="scheduler-tab">
      <article className="panel">
        <header>
          <h2>Планировщик задач</h2>
          <p>Определяйте фоновые задания, управляйте приоритетами и контролируйте прогресс.</p>
        </header>
        <form className="form-grid" onSubmit={handleCreate}>
          <label htmlFor="task-name">Имя задачи</label>
          <input
            id="task-name"
            value={name}
            onChange={(event) => setName(event.target.value)}
            disabled={isSubmitting}
            placeholder="Например: vm.run"
          />
          <label htmlFor="task-priority">Приоритет</label>
          <input
            id="task-priority"
            value={priority}
            onChange={(event) => setPriority(event.target.value)}
            inputMode="numeric"
            disabled={isSubmitting}
          />
          <label htmlFor="task-schedule">Расписание</label>
          <input
            id="task-schedule"
            value={schedule}
            onChange={(event) => setSchedule(event.target.value)}
            disabled={isSubmitting}
            placeholder="immediate | cron выражение"
          />
          <label htmlFor="task-payload">Payload (JSON)</label>
          <textarea
            id="task-payload"
            value={payload}
            onChange={(event) => setPayload(event.target.value)}
            disabled={isSubmitting}
          />
          <button type="submit" disabled={isSubmitting}>
            {isSubmitting ? <Spinner /> : "Запланировать"}
          </button>
        </form>
      </article>
      <article className="panel">
        <header className="inline-actions">
          <div>
            <h3>Очередь задач</h3>
            <p>Активные и завершённые задания control-plane.</p>
          </div>
          <div className="panel-tools">
            <AutoRefreshControl
              enabled={autoRefresh}
              onToggle={(next) => setAutoRefresh(next)}
              lastUpdated={lastUpdated}
              intervalMs={autoRefresh ? 10000 : undefined}
              isLoading={isLoading}
            />
            <button type="button" className="inline" onClick={() => void refresh()} disabled={isLoading}>
              {isLoading ? <Spinner /> : "Обновить"}
            </button>
          </div>
        </header>
        <DataTable
          columns={[
            { key: "id", title: "ID" },
            { key: "name", title: "Имя" },
            { key: "status", title: "Статус" },
            {
              key: "priority",
              title: "Приоритет",
              render: (_value, row) => row.priority ?? "—"
            },
            {
              key: "progress",
              title: "Прогресс",
              render: (value) => (typeof value === "number" ? `${Math.round(value * 100)} %` : "—")
            },
            {
              key: "nextRunAt",
              title: "Следующий запуск",
              render: (_value, row) => formatTimestamp(row.nextRunAt)
            },
            {
              key: "actions",
              title: "Действия",
              render: (_value, row) => (
                <div className="button-group">
                  <button type="button" onClick={() => updateTask(row.id, { priority: (row.priority ?? 0) + 1 })}>
                    ↑ приоритет
                  </button>
                  <button type="button" onClick={() => updateTask(row.id, { status: "queued" })}>
                    Повторить
                  </button>
                  <button type="button" onClick={() => cancelTask(row.id)}>
                    Отменить
                  </button>
                </div>
              )
            }
          ]}
          data={tasks}
          emptyMessage="Задачи не найдены"
        />
      </article>
      <article className="panel">
        <header>
          <h3>Сводка</h3>
          <p>Количество активных задач и их приоритеты.</p>
        </header>
        <div className="metrics-grid">
          <div className="metric-card">
            <span>Активных задач</span>
            <strong>{activeTasks.length}</strong>
          </div>
          <div className="metric-card">
            <span>Макс. приоритет</span>
            <strong>
              {activeTasks.length ? Math.max(...activeTasks.map((task) => task.effectivePriority)) : "—"}
            </strong>
          </div>
          <div className="metric-card">
            <span>Мин. приоритет</span>
            <strong>
              {activeTasks.length ? Math.min(...activeTasks.map((task) => task.effectivePriority)) : "—"}
            </strong>
          </div>
        </div>
      </article>
    </section>
  );
}
