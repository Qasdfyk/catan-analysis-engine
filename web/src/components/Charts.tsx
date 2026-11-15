// Small hand-drawn charts and number tiles. No chart library: each is a few
// divs or one SVG, styled from style.css.

import { useEffect, useRef, useState } from 'react';
import { useCountUp } from '../hooks';

export const fmt = (n: number, digits = 0) =>
  n.toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits }).replace(/,/g, '\u202f'); // narrow no-break space: 10 800

/** A big number with a caption, counted up when it scrolls into view. */
export function Stat({ value, digits = 0, suffix = '', label, tone }: {
  value: number;
  digits?: number;
  suffix?: string;
  label: string;
  tone?: 'gold' | 'green';
}) {
  const [ref, shown] = useCountUp<HTMLDivElement>(value);
  return (
    <div className="stat" ref={ref}>
      <div className={`stat-value${tone ? ` ${tone}` : ''}`}>
        {fmt(shown, digits)}
        {suffix}
      </div>
      <div className="stat-label">{label}</div>
    </div>
  );
}

/** True once the element has been on screen; drives entry animations. */
function useSeen<T extends Element>() {
  const ref = useRef<T>(null);
  const [seen, setSeen] = useState(false);
  useEffect(() => {
    if (!ref.current) return;
    const observer = new IntersectionObserver(([e]) => e.isIntersecting && (setSeen(true), observer.disconnect()), {
      threshold: 0.15,
    });
    observer.observe(ref.current);
    return () => observer.disconnect();
  }, []);
  return [ref, seen] as const;
}

export interface Bar {
  label: string;
  value: number;
  note?: string;
  tone?: 'gold' | 'green' | 'muted';
}

/**
 * Horizontal bars on a log scale: 2.4 and 10 800 games/s differ by 4 500×,
 * and on a linear scale the Python bars would be invisible. Each gridline is
 * ten times the one before.
 */
export function LogBars({ bars, max = 100_000, unit }: { bars: Bar[]; max?: number; unit: string }) {
  const [ref, seen] = useSeen<HTMLDivElement>();
  const decades = Math.ceil(Math.log10(max));
  const width = (v: number) => `${Math.max(1.5, (Math.log10(Math.max(1, v)) / decades) * 100)}%`;
  return (
    <div className={`logbars${seen ? ' seen' : ''}`} ref={ref}>
      {bars.map((b) => (
        <div className="logbar" key={b.label}>
          <div className="logbar-label">
            <span>{b.label}</span>
            {b.note && <span className="muted small">{b.note}</span>}
          </div>
          <div className="logbar-track">
            {Array.from({ length: decades - 1 }, (_, i) => (
              <span key={i} className="gridline" style={{ left: `${((i + 1) / decades) * 100}%` }} />
            ))}
            <div className={`logbar-fill ${b.tone ?? 'muted'}`} style={{ width: seen ? width(b.value) : 0 }} />
          </div>
          <div className="logbar-value">{fmt(b.value, b.value < 100 ? 1 : 0)}</div>
        </div>
      ))}
      <div className="logbar-axis">
        <span />
        <div className="logbar-ticks">
          {Array.from({ length: decades + 1 }, (_, i) => (
            <span key={i} style={{ left: `${(i / decades) * 100}%` }}>
              {tick(i)}
            </span>
          ))}
        </div>
        <span className="muted small">{unit}</span>
      </div>
    </div>
  );
}

const tick = (decade: number) => ['1', '10', '100', '1k', '10k', '100k', '1M'][decade] ?? `1e${decade}`;
