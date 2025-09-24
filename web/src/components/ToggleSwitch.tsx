// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import type { ChangeEvent } from "react";

interface ToggleSwitchProps {
  id: string;
  checked: boolean;
  onChange: (value: boolean) => void;
  label: string;
  description?: string;
}

export function ToggleSwitch({ id, checked, onChange, label, description }: ToggleSwitchProps) {
  const handleChange = (event: ChangeEvent<HTMLInputElement>) => {
    onChange(event.target.checked);
  };

  return (
    <label className="toggle-switch" htmlFor={id}>
      <input id={id} type="checkbox" checked={checked} onChange={handleChange} />
      <span className="toggle-switch__control" aria-hidden="true" />
      <span className="toggle-switch__content">
        <span className="toggle-switch__label">{label}</span>
        {description ? <span className="toggle-switch__description">{description}</span> : null}
      </span>
    </label>
  );
}
