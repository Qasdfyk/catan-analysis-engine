// The replay viewer: open a record (or the example), step through it by
// hand or let it play.

import { useEffect, useMemo, useState } from 'react';
import { GameBoard } from '../components/GameBoard';
import { MoveLog, Players, Status } from '../components/GamePanels';
import { DiceChart, IncomeChart, PointsChart } from '../components/ReplayCharts';
import { EXAMPLES, exampleRecord, readRecordBytes } from '../engine';
import { useOpenGame, usePlayback } from '../hooks';
import { turnStart } from '../nav';
import { gameStats } from '../stats';

const SPEEDS = [0.5, 1, 2, 4];

interface Source {
  text: string;
  label: string;
}

export function Replay() {
  const [source, setSource] = useState<Source | null>(null);
  const [message, setMessage] = useState('');
  const { game, topo, error } = useOpenGame(source?.text ?? null);
  const playback = usePlayback(game);
  const { pos, go, playing, setPlaying, speed, setSpeed } = playback;
  // Every state of the game is read once here; the charts then cut at pos.
  const stats = useMemo(() => (game ? gameStats(game) : null), [game]);

  // The example opens by itself, so the tab is never empty.
  useEffect(() => {
    exampleRecord().then(
      (text) => setSource((s) => s ?? { text, label: EXAMPLES[0].label }),
      (e) => setMessage(`Cannot load the example game: ${e.message}`),
    );
  }, []);

  const openFile = async (file: File) => {
    try {
      setSource({ text: await readRecordBytes(await file.arrayBuffer()), label: file.name });
      setMessage('');
    } catch (e) {
      setMessage(`${file.name}: cannot read it (${e instanceof Error ? e.message : String(e)}).`);
    }
  };

  // A file dropped anywhere on the page opens.
  useEffect(() => {
    const over = (e: DragEvent) => e.preventDefault();
    const drop = (e: DragEvent) => {
      e.preventDefault();
      const file = e.dataTransfer?.files[0];
      if (file) void openFile(file);
    };
    addEventListener('dragover', over);
    addEventListener('drop', drop);
    return () => {
      removeEventListener('dragover', over);
      removeEventListener('drop', drop);
    };
  }, []);

  useEffect(() => {
    if (!game) return;
    const keys: Record<string, () => number> = {
      ArrowLeft: () => pos - 1,
      ArrowRight: () => pos + 1,
      PageUp: () => turnStart(game.record.events, pos, -1),
      PageDown: () => turnStart(game.record.events, pos, 1),
      Home: () => 0,
      End: () => Infinity,
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement && e.target.type !== 'range') return;
      if (e.key === ' ' && !(e.target instanceof HTMLButtonElement)) {
        e.preventDefault();
        setPlaying(!playing);
        return;
      }
      const to = keys[e.key];
      if (to) {
        e.preventDefault();
        setPlaying(false);
        go(to());
      }
    };
    addEventListener('keydown', onKey);
    return () => removeEventListener('keydown', onKey);
  }, [game, pos, go, playing, setPlaying]);

  const step = (to: number) => {
    setPlaying(false);
    go(to);
  };
  const togglePlay = () => {
    if (!game) return;
    if (!playing && pos >= game.stateCount - 1) go(0);
    setPlaying(!playing);
  };

  const shownError = message || (error && `${source?.label}: ${error}`) || (game?.error && `${source?.label}: the engine stopped early: ${game.error}`);

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Replay</h1>
          <p className="lede">
            Step through a finished game. The rules engine rebuilds every state from the record, here in WebAssembly; the
            record never leaves your computer.
          </p>
        </div>
        <div className="open">
          <label className="button primary" htmlFor="file">
            Open a game record…
          </label>
          <input
            id="file"
            type="file"
            accept=".json,.gz,application/json,application/gzip"
            hidden
            onChange={(e) => {
              const file = e.target.files?.[0];
              if (file) void openFile(file);
              e.target.value = '';
            }}
          />
          <select
            aria-label="Example games"
            value=""
            onChange={(e) => {
              const example = EXAMPLES.find((x) => x.file === e.target.value);
              if (!example) return;
              exampleRecord(example.file).then(
                (text) => {
                  setSource({ text, label: example.label });
                  setMessage('');
                },
                (err) => setMessage(`Cannot load ${example.label}: ${err.message}`),
              );
            }}
          >
            <option value="" disabled>
              Example games…
            </option>
            {EXAMPLES.map((x) => (
              <option key={x.file} value={x.file}>
                {x.label} ({x.note})
              </option>
            ))}
          </select>
        </div>
      </div>
      <p className="hint">
        Records are the <code>.json</code> or <code>.json.gz</code> files catan-project saves after every game. Drop one
        anywhere on the page.
      </p>

      {shownError && (
        <p className="message error" role="status">
          {shownError}
        </p>
      )}

      {game && topo ? (
        <div className="viewer">
          <section className="board-pane">
            <GameBoard game={game} topo={topo} pos={pos} />
            <div className="controls">
              <button type="button" title="Start (Home)" onClick={() => step(0)}>
                ⏮
              </button>
              <button type="button" title="Previous turn (Page Up)" onClick={() => step(turnStart(game.record.events, pos, -1))}>
                ⏪
              </button>
              <button type="button" title="Previous move (←)" onClick={() => step(pos - 1)}>
                ◀
              </button>
              <button type="button" className="play" title="Play / pause (Space)" onClick={togglePlay}>
                {playing ? '❚❚' : '▶'}
              </button>
              <button type="button" title="Next move (→)" onClick={() => step(pos + 1)}>
                ▶❘
              </button>
              <button type="button" title="Next turn (Page Down)" onClick={() => step(turnStart(game.record.events, pos, 1))}>
                ⏩
              </button>
              <button type="button" title="End (End)" onClick={() => step(Infinity)}>
                ⏭
              </button>
              <select value={speed} onChange={(e) => setSpeed(Number(e.target.value))} aria-label="Playback speed">
                {SPEEDS.map((s) => (
                  <option key={s} value={s}>
                    {s}×
                  </option>
                ))}
              </select>
            </div>
            <input
              className="scrubber"
              type="range"
              min={0}
              max={game.stateCount - 1}
              value={pos}
              aria-label="Move"
              onChange={(e) => step(Number(e.target.value))}
            />
            <p className="position">
              Move {pos} of {game.stateCount - 1} · turn {game.state(pos).turn} · {source?.label}
            </p>
            {stats && <PointsChart stats={stats} players={game.record.players} pos={pos} onSeek={step} />}
          </section>
          <aside className="side">
            <Status game={game} pos={pos} />
            <Players state={game.state(pos)} players={game.record.players} />
            <MoveLog game={game} pos={pos} onPick={step} />
          </aside>
          {stats && (
            <section className="charts">
              <DiceChart stats={stats} players={game.record.players} pos={pos} />
              <IncomeChart stats={stats} players={game.record.players} pos={pos} />
            </section>
          )}
        </div>
      ) : (
        !shownError && <div className="board-area placeholder viewer-placeholder" aria-busy="true" />
      )}
    </>
  );
}
