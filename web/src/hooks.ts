// React hooks shared by the pages: the tab in the URL, a record opened in the
// engine, autoplay through a game, and numbers that count up.

import { useCallback, useEffect, useRef, useState, type RefObject } from 'react';
import { OpenGame, loadEngine, topology } from './engine';
import { playbackDelay, tabFromHash, type Tab } from './nav';
import type { Topology } from './types';

/** The tab named in the URL hash; follows back/forward and links. */
export function useHashTab(): Tab {
  const [tab, setTab] = useState(() => tabFromHash(location.hash));
  useEffect(() => {
    const onChange = () => {
      setTab(tabFromHash(location.hash));
      window.scrollTo({ top: 0 });
    };
    addEventListener('hashchange', onChange);
    return () => removeEventListener('hashchange', onChange);
  }, []);
  return tab;
}

export interface Opened {
  game: OpenGame | null;
  topo: Topology | null;
  /** Why the record could not be opened; '' when it was (or nothing was asked). */
  error: string;
}

/**
 * Opens `text` (a record) in the engine. The C++ side of the previous game is
 * freed when the text changes or the component goes away: the garbage
 * collector would not free WebAssembly memory.
 */
export function useOpenGame(text: string | null): Opened {
  const [opened, setOpened] = useState<Opened>({ game: null, topo: null, error: '' });
  useEffect(() => {
    if (text === null) return;
    let game: OpenGame | null = null;
    let cancelled = false;
    (async () => {
      try {
        const [engine, topo] = await Promise.all([loadEngine(), topology()]);
        if (cancelled) return;
        game = new OpenGame(engine, text);
        if (game.stateCount === 0) {
          const error = `not a game record the engine can read (${game.error})`;
          game.close();
          game = null;
          setOpened({ game: null, topo, error });
        } else {
          setOpened({ game, topo, error: '' });
        }
      } catch (e) {
        if (!cancelled) setOpened({ game: null, topo: null, error: e instanceof Error ? e.message : String(e) });
      }
    })();
    return () => {
      cancelled = true;
      game?.close();
    };
  }, [text]);
  return opened;
}

export interface Playback {
  /** The state shown: 0 before the first event, i after event i - 1. */
  pos: number;
  go: (to: number) => void;
  playing: boolean;
  setPlaying: (on: boolean) => void;
  speed: number;
  setSpeed: (x: number) => void;
}

/**
 * Position in a game plus autoplay. Each step waits playbackDelay(event) /
 * speed. With `loop`, the last state stays up for a few seconds and the game
 * starts again.
 */
export function usePlayback(game: OpenGame | null, { autoplay = false, loop = false } = {}): Playback {
  const [pos, setPos] = useState(0);
  const [playing, setPlaying] = useState(autoplay);
  const [speed, setSpeed] = useState(1);
  const last = game ? game.stateCount - 1 : 0;

  // A new game starts from the beginning.
  useEffect(() => {
    setPos(0);
    setPlaying(autoplay);
  }, [game, autoplay]);

  const go = useCallback((to: number) => setPos(Math.max(0, Math.min(last, to))), [last]);

  useEffect(() => {
    if (!game || !playing) return;
    if (pos >= last) {
      if (!loop) {
        setPlaying(false);
        return;
      }
      const id = setTimeout(() => setPos(0), 5000);
      return () => clearTimeout(id);
    }
    const id = setTimeout(() => setPos((p) => Math.min(last, p + 1)), playbackDelay(game.record.events[pos]) / speed);
    return () => clearTimeout(id);
  }, [game, playing, pos, last, loop, speed]);

  return { pos, go, playing, setPlaying, speed, setSpeed };
}

const reducedMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;

/** `target`, counted up from 0 over `ms` once the element is on screen. */
export function useCountUp<T extends Element>(target: number, ms = 1400): [RefObject<T>, number] {
  const ref = useRef<T>(null);
  const [value, setValue] = useState(() => (reducedMotion() ? target : 0));
  useEffect(() => {
    if (reducedMotion() || !ref.current) {
      setValue(target);
      return;
    }
    let frame = 0;
    const observer = new IntersectionObserver(([entry]) => {
      if (!entry.isIntersecting) return;
      observer.disconnect();
      const start = performance.now();
      const step = (now: number) => {
        const t = Math.min(1, (now - start) / ms);
        setValue(target * (1 - (1 - t) ** 3)); // ease-out cubic
        if (t < 1) frame = requestAnimationFrame(step);
      };
      frame = requestAnimationFrame(step);
    });
    observer.observe(ref.current);
    return () => {
      observer.disconnect();
      cancelAnimationFrame(frame);
    };
  }, [target, ms]);
  return [ref, value];
}
