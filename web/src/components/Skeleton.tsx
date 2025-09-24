// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import type { CSSProperties } from "react";

interface SkeletonProps {
  width?: string | number;
  height?: string | number;
  rounded?: boolean;
  style?: CSSProperties;
  className?: string;
}

function toStyleSize(value?: string | number) {
  if (typeof value === "number") {
    return `${value}px`;
  }
  return value;
}

export function Skeleton({ width = "100%", height = 16, rounded = true, className, style }: SkeletonProps) {
  return (
    <span
      className={`skeleton ${rounded ? "skeleton--rounded" : ""} ${className ?? ""}`.trim()}
      style={{ width: toStyleSize(width), height: toStyleSize(height), ...style }}
      aria-hidden="true"
    />
  );
}

interface SkeletonLinesProps {
  count?: number;
  minWidth?: number;
}

export function SkeletonLines({ count = 3, minWidth = 40 }: SkeletonLinesProps) {
  return (
    <div className="skeleton-lines" aria-hidden="true">
      {Array.from({ length: count }).map((_, index) => (
        <Skeleton key={index} width={`${Math.max(minWidth, 100 - index * 8)}%`} />
      ))}
    </div>
  );
}

interface SkeletonGridProps {
  columns?: number;
  rows?: number;
}

export function SkeletonGrid({ columns = 3, rows = 2 }: SkeletonGridProps) {
  return (
    <div className="skeleton-grid" aria-hidden="true">
      {Array.from({ length: columns * rows }).map((_, index) => (
        <Skeleton key={index} height={14} />
      ))}
    </div>
  );
}
