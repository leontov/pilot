// Copyright (c) 2024 Кочуров Владислав Евгеньевич

export interface DialogRequest {
  input: string;
}

export interface DialogResponse {
  answer: string;
  trace?: unknown;
  timestamp?: string;
}

export interface VmRunRequest {
  program: number[];
  gasLimit?: number;
}

export interface VmRunResponse {
  status?: string;
  result?: unknown;
  trace?: unknown;
  steps?: number;
  gasUsed?: number;
}

export interface MemoryValue {
  key: string;
  value: string;
}

export interface MemoryProgram {
  id: string;
  description?: string;
  score?: number;
  bytecode?: number[];
}

export interface MemoryResponse {
  values?: MemoryValue[];
  programs?: MemoryProgram[];
}

export interface ProgramSubmitRequest {
  bytecode: number[];
  notes?: string;
}

export interface ProgramSubmitResponse {
  programId?: string;
  poe?: number;
  mdl?: number;
  score?: number;
  accepted?: boolean;
  trace?: unknown;
}

export interface ChainSubmitRequest {
  programId: string;
}

export interface ChainSubmitResponse {
  status: string;
  blockId?: string;
  position?: number;
  poe?: number;
  mdlDelta?: number;
}

export interface MemoryStats {
  total?: number;
  used?: number;
}

export interface PeerInfo {
  id: string;
  role?: string;
  latency?: number;
  status?: string;
  address?: string;
  score?: number;
}

export interface PeerCommandRequest {
  score?: number;
  role?: string;
  quarantine?: boolean;
  notes?: string;
}

export interface PeerCommandResponse {
  acknowledged: boolean;
  peer?: PeerInfo;
  message?: string;
}

export interface HealthResponse {
  uptime: number;
  memory: MemoryStats;
  peers?: PeerInfo[];
  blocks?: number;
  version?: string;
}

export interface MetricsResponse {
  uptime?: number;
  memory?: MemoryStats;
  peers?: PeerInfo[];
  blocks?: number;
  tasksInFlight?: number;
  lastBlockTime?: string;
  [key: string]: unknown;
}

export class ApiError<T = unknown> extends Error {
  readonly status: number;
  readonly payload?: T;

  constructor(message: string, status: number, payload?: T) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.payload = payload;
  }
}

const STATUS_MESSAGES: Record<number, string> = {
  400: "Некорректный запрос к API диалога. Проверьте отправляемые данные.",
  401: "Требуется аутентификация для обращения к ядру Kolibri Ω.",
  403: "Доступ к диалоговому эндпоинту запрещён.",
  404: "Эндпоинт диалога недоступен, проверьте запуск ядра Kolibri Ω.",
  409: "Конфликт при обращении к диалоговому API. Повторите попытку позже.",
  429: "Превышен лимит обращений к ядру Kolibri Ω. Подождите перед новой попыткой.",
  500: "Внутренняя ошибка ядра Kolibri Ω. Проверьте логи сервиса.",
  502: "Некорректный ответ от backend. Убедитесь, что сервис запущен корректно.",
  503: "Диалоговый сервис временно недоступен. Попробуйте повторить запрос позже.",
  504: "Превышено время ожидания ответа от ядра Kolibri Ω."
};

export function formatApiError(error: unknown): string {
  if (error instanceof ApiError) {
    const predefined = STATUS_MESSAGES[error.status];
    if (predefined) {
      return predefined;
    }
    const fallback = error.message || "Запрос к API завершился ошибкой.";
    return `Ошибка API (${error.status}). ${fallback}`;
  }

  if (error instanceof Error) {
    return error.message;
  }

  return "Неизвестная ошибка";
}

interface RequestOptions extends RequestInit {
  skipJson?: boolean;
}

type StreamCleanup = () => void;

interface StreamOptions<T> {
  onMessage: (payload: T) => void;
  onError?: (error: Error) => void;
  onOpen?: () => void;
  signal?: AbortSignal;
  preferWebSocket?: boolean;
}

export interface ScheduledTask {
  id: string;
  name: string;
  status: "queued" | "running" | "success" | "failed" | "cancelled" | string;
  priority?: number;
  assignedTo?: string;
  createdAt?: string;
  updatedAt?: string;
  nextRunAt?: string;
  lastRunAt?: string;
  progress?: number;
  metadata?: Record<string, unknown>;
}

export interface TaskCreateRequest {
  name: string;
  payload?: unknown;
  priority?: number;
  schedule?: string;
  tags?: string[];
}

export interface TaskUpdateRequest {
  status?: ScheduledTask["status"];
  priority?: number;
  payload?: unknown;
}

export interface TaskActionResponse {
  acknowledged: boolean;
  task?: ScheduledTask;
  message?: string;
}

export interface MonitoringAlert {
  id: string;
  severity: "info" | "warning" | "critical" | string;
  title: string;
  description?: string;
  raisedAt: string;
  clearedAt?: string;
  relatedTaskId?: string;
}

export interface MonitoringTimelineEntry {
  timestamp: string;
  label: string;
  value?: number;
  metadata?: Record<string, unknown>;
}

export interface MonitoringSnapshot {
  metrics: MetricsResponse;
  health: HealthResponse;
  alerts: MonitoringAlert[];
  timeline?: MonitoringTimelineEntry[];
}

export interface VmTraceEvent {
  type: "state" | "log" | "result" | "error" | "complete" | string;
  timestamp?: string;
  payload?: unknown;
  step?: number;
}

export interface VmStreamSession {
  sessionId: string;
  close: () => void;
}

export interface VmStreamRequest extends VmRunRequest {
  programId?: string;
  comment?: string;
}

export interface VmStreamOptions {
  onEvent: (event: VmTraceEvent) => void;
  onError?: (error: Error) => void;
  preferWebSocket?: boolean;
  signal?: AbortSignal;
}

const DEFAULT_BASE_URL = (() => {
  if (typeof window !== "undefined" && typeof window === "object") {
    const fromWindow = (window as Record<string, unknown>).KOLIBRI_API_BASE;
    if (typeof fromWindow === "string" && fromWindow.trim()) {
      return fromWindow.trim();
    }
  }
  if (typeof import.meta !== "undefined" && (import.meta as Record<string, unknown>).env) {
    const candidate = (import.meta as { env: Record<string, unknown> }).env.VITE_API_BASE;
    if (typeof candidate === "string" && candidate.trim()) {
      return candidate.trim();
    }
  }
  const maybeProcess =
    typeof globalThis !== "undefined" &&
    (globalThis as { process?: { env?: Record<string, unknown> } }).process;
  if (maybeProcess && typeof maybeProcess.env?.VITE_API_BASE === "string") {
    return maybeProcess.env.VITE_API_BASE.trim();
  }
  return "";
})();

function normalizeBaseUrl(baseUrl: string): string {
  if (!baseUrl) {
    return "";
  }
  return baseUrl.endsWith("/") ? baseUrl.slice(0, -1) : baseUrl;
}

function ensureAbsoluteUrl(baseUrl: string, path: string): string {
  const normalizedPath = /^https?:/i.test(path) || /^wss?:/i.test(path)
    ? path
    : (() => {
        const prefixed = path.startsWith("/") ? path : `/${path}`;
        if (baseUrl) {
          return `${baseUrl}${prefixed}`;
        }
        if (typeof window !== "undefined") {
          return new URL(prefixed, window.location.origin).toString();
        }
        return new URL(prefixed, "http://localhost").toString();
      })();

  if (/^https?:/i.test(normalizedPath) || /^wss?:/i.test(normalizedPath)) {
    return normalizedPath;
  }
  if (typeof window !== "undefined") {
    return new URL(normalizedPath, window.location.origin).toString();
  }
  return new URL(normalizedPath, baseUrl || "http://localhost").toString();
}

function toWebSocketUrl(url: string): string {
  const parsed = new URL(url, typeof window !== "undefined" ? window.location.origin : "http://localhost");
  parsed.protocol = parsed.protocol === "https:" ? "wss:" : "ws:";
  return parsed.toString();
}

export class KolibriClient {
  private readonly baseUrl: string;

  constructor(baseUrl = DEFAULT_BASE_URL) {
    this.baseUrl = normalizeBaseUrl(baseUrl);
  }

  private buildUrl(path: string): string {
    if (/^https?:/i.test(path) || /^wss?:/i.test(path)) {
      return path;
    }
    const normalizedPath = path.startsWith("/") ? path : `/${path}`;
    if (!this.baseUrl) {
      return normalizedPath;
    }
    return `${this.baseUrl}${normalizedPath}`;
  }

  private openStream<T>(path: string, options: StreamOptions<T>): StreamCleanup {
    if (typeof window === "undefined") {
      throw new Error("Streaming доступно только в браузере");
    }

    const absoluteUrl = ensureAbsoluteUrl(this.baseUrl, this.buildUrl(path));
    const supportsEventSource = typeof window.EventSource !== "undefined";
    const supportsWebSocket = typeof window.WebSocket !== "undefined";
    const preferWebSocket = options.preferWebSocket === true;

    const handleMessage = (data: string) => {
      let parsed: unknown = data;
      try {
        parsed = data ? JSON.parse(data) : data;
      } catch (error) {
        // keep original string
      }
      options.onMessage(parsed as T);
    };

    const handleError = (reason: unknown) => {
      if (!options.onError) {
        return;
      }
      if (reason instanceof Error) {
        options.onError(reason);
      } else if (typeof reason === "string") {
        options.onError(new Error(reason));
      } else {
        options.onError(new Error("Неизвестная ошибка стрима"));
      }
    };

    if (supportsEventSource && !preferWebSocket) {
      const eventSource = new EventSource(absoluteUrl);
      const abortHandler = () => {
        eventSource.close();
      };
      eventSource.onmessage = (event) => handleMessage(event.data);
      eventSource.onerror = () => handleError(new Error("Ошибка SSE соединения"));
      eventSource.onopen = () => options.onOpen?.();
      options.signal?.addEventListener("abort", abortHandler);
      return () => {
        eventSource.close();
        options.signal?.removeEventListener("abort", abortHandler);
      };
    }

    if (supportsWebSocket) {
      const wsUrl = toWebSocketUrl(absoluteUrl);
      const socket = new WebSocket(wsUrl);
      const abortHandler = () => {
        socket.close();
      };
      socket.onopen = () => options.onOpen?.();
      socket.onmessage = (event) => {
        const payload = typeof event.data === "string" ? event.data : String(event.data);
        handleMessage(payload);
      };
      socket.onerror = () => handleError(new Error("Ошибка WebSocket соединения"));
      options.signal?.addEventListener("abort", abortHandler);
      return () => {
        socket.close();
        options.signal?.removeEventListener("abort", abortHandler);
      };
    }

    throw new Error("Браузер не поддерживает SSE или WebSocket");
  }

  private async request<TResponse = unknown>(path: string, init: RequestOptions = {}): Promise<TResponse> {
    const headers = new Headers(init.headers ?? {});
    if (!(init.body instanceof FormData) && !headers.has("Content-Type") && init.method && init.method !== "GET") {
      headers.set("Content-Type", "application/json");
    }

    const response = await fetch(this.buildUrl(path), { ...init, headers });
    if (!response.ok) {
      let payload: unknown;
      try {
        payload = await response.json();
      } catch (error) {
        // ignore JSON parse errors
      }
      const message = payload && typeof payload === "object" && "error" in payload ? String((payload as { error: unknown }).error) : response.statusText;
      throw new ApiError(message || "Request failed", response.status, payload);
    }

    if (init.skipJson || response.status === 204) {
      return undefined as TResponse;
    }

    const text = await response.text();
    if (!text) {
      return undefined as TResponse;
    }

    return JSON.parse(text) as TResponse;
  }

  dialog(request: DialogRequest): Promise<DialogResponse> {
    return this.request<DialogResponse>("/api/v1/dialog", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  runProgram(request: VmRunRequest): Promise<VmRunResponse> {
    return this.request<VmRunResponse>("/api/v1/vm/run", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  getMemory(prefix: string): Promise<MemoryResponse> {
    const search = new URLSearchParams({ prefix });
    return this.request<MemoryResponse>(`/api/v1/fkv/get?${search.toString()}`, {
      method: "GET"
    });
  }

  submitProgram(request: ProgramSubmitRequest): Promise<ProgramSubmitResponse> {
    return this.request<ProgramSubmitResponse>("/api/v1/program/submit", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  submitChain(request: ChainSubmitRequest): Promise<ChainSubmitResponse> {
    return this.request<ChainSubmitResponse>("/api/v1/chain/submit", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  health(): Promise<HealthResponse> {
    return this.request<HealthResponse>("/api/v1/health", {
      method: "GET"
    });
  }

  metrics(): Promise<MetricsResponse> {
    return this.request<MetricsResponse>("/api/v1/metrics", {
      method: "GET"
    });
  }

  listTasks(): Promise<ScheduledTask[]> {
    return this.request<ScheduledTask[]>("/api/v1/control/tasks", {
      method: "GET"
    });
  }

  createTask(request: TaskCreateRequest): Promise<TaskActionResponse> {
    return this.request<TaskActionResponse>("/api/v1/control/tasks", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  updateTask(taskId: string, request: TaskUpdateRequest): Promise<TaskActionResponse> {
    return this.request<TaskActionResponse>(`/api/v1/control/tasks/${encodeURIComponent(taskId)}`, {
      method: "PATCH",
      body: JSON.stringify(request)
    });
  }

  cancelTask(taskId: string): Promise<TaskActionResponse> {
    return this.request<TaskActionResponse>(`/api/v1/control/tasks/${encodeURIComponent(taskId)}`, {
      method: "DELETE"
    });
  }

  monitoringSnapshot(): Promise<MonitoringSnapshot> {
    return this.request<MonitoringSnapshot>("/api/v1/control/monitoring", {
      method: "GET"
    });
  }

  acknowledgeAlert(alertId: string): Promise<{ acknowledged: boolean }> {
    return this.request<{ acknowledged: boolean }>(`/api/v1/control/monitoring/alerts/${encodeURIComponent(alertId)}/ack`, {
      method: "POST"
    });
  }

  updatePeer(peerId: string, request: PeerCommandRequest): Promise<PeerCommandResponse> {
    return this.request<PeerCommandResponse>(`/api/v1/control/cluster/peers/${encodeURIComponent(peerId)}`, {
      method: "PATCH",
      body: JSON.stringify(request)
    });
  }

  disconnectPeer(peerId: string): Promise<PeerCommandResponse> {
    return this.request<PeerCommandResponse>(`/api/v1/control/cluster/peers/${encodeURIComponent(peerId)}`, {
      method: "DELETE"
    });
  }

  async streamVmExecution(request: VmStreamRequest, options: VmStreamOptions): Promise<VmStreamSession> {
    if (options.signal?.aborted) {
      throw new Error("Запрошенный поток уже отменён");
    }

    const response = await this.request<{ sessionId: string }>("/api/v1/vm/stream", {
      method: "POST",
      body: JSON.stringify(request),
      signal: options.signal
    });

    if (!response.sessionId) {
      throw new Error("Сервер не вернул идентификатор сессии потока");
    }

    const controller = new AbortController();
    const cleanup = this.openStream<VmTraceEvent>(`/api/v1/vm/stream/${encodeURIComponent(response.sessionId)}`, {
      onMessage: options.onEvent,
      onError: options.onError,
      preferWebSocket: options.preferWebSocket,
      signal: controller.signal
    });

    const close = () => {
      cleanup();
      controller.abort();
      options.signal?.removeEventListener("abort", close);
    };

    options.signal?.addEventListener("abort", close, { once: true });

    return { sessionId: response.sessionId, close };
  }
}

export const apiClient = new KolibriClient();
