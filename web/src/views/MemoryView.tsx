import { FormEvent, useMemo, useState } from "react";
import { apiClient, MemoryProgram, MemoryValue } from "../api/client";
import { DataTable } from "../components/DataTable";
import { Spinner } from "../components/Spinner";
import { useNotifications } from "../components/NotificationCenter";

interface NormalizedProgram extends MemoryProgram {
  displayBytecode?: string;
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
        <DataTable
          columns={[
            { key: "key", title: "Префикс" },
            { key: "value", title: "Значение" }
          ]}
          data={values}
          emptyMessage="Значения не найдены"
        />
      </article>
      <article className="panel">
        <header>
          <h3>Связанные программы</h3>
          <p>Релевантные байткоды и их метрики.</p>
        </header>
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
      </article>
    </section>
  );
}
