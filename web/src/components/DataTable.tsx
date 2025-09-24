// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import type { ReactNode } from "react";

export interface Column<T> {
  key: keyof T | string;
  title: ReactNode;
  render?: (value: T[keyof T], row: T) => ReactNode;
  width?: string;
}

export interface DataTableProps<T> {
  columns: Column<T>[];
  data: T[];
  emptyMessage?: ReactNode;
}

export function DataTable<T>({ columns, data, emptyMessage }: DataTableProps<T>) {
  if (!data.length) {
    return <div className="empty-state">{emptyMessage ?? "Данные отсутствуют"}</div>;
  }

  return (
    <table className="data-table">
      <thead>
        <tr>
          {columns.map((column) => (
            <th key={String(column.key)} style={column.width ? { width: column.width } : undefined} scope="col">
              {column.title}
            </th>
          ))}
        </tr>
      </thead>
      <tbody>
        {data.map((row, rowIndex) => (
          <tr key={rowIndex}>
            {columns.map((column) => {
              const value = (row as Record<string, unknown>)[column.key as string];
              return <td key={String(column.key)}>{column.render ? column.render(value as T[keyof T], row) : (value as ReactNode)}</td>;
            })}
          </tr>
        ))}
      </tbody>
    </table>
  );
}
