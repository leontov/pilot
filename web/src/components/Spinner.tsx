import type { HTMLAttributes } from "react";

export function Spinner({ className = "", ...rest }: HTMLAttributes<HTMLDivElement>) {
  return <div className={`spinner ${className}`.trim()} aria-hidden="true" {...rest} />;
}
