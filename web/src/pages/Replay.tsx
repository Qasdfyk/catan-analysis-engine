// The replay viewer: open a record (a file, an example, or the game the
// opening page hands over), step through it by hand or let it play.

import { useEffect, useMemo, useState } from 'react';
import { readRecord } from '../adapters';
import { GameBoard } from '../components/GameBoard';
import { MoveLog, Players, Status } from '../components/GamePanels';
import { LuckPanel, WinChart } from '../components/WinChances';
import { useWinChances } from '../chances';
import { SetupHeat, SetupPanel } from '../components/SetupReview';
import { BestMoveSpot, MoveReviewPanel } from '../components/MoveReview';
import { byEvent, useMoveGrades } from '../grades';
import { PLAYER_COLORS } from '@platform/utils/labels';
import { focusMove, useSetupReview } from '../setup';
import { EXAMPLES, exampleRecord } from '../engine';
import { expectsHandoff, receiveFromOpener } from '../handoff';
import { useOpenGame, usePlayback } from '../hooks';
import { reviewSeat, turnStart } from '../nav';
import { seatName } from '../components/GamePanels';

const SPEEDS = [0.5, 1, 2, 4];

interface Source {
  text: string;
  label: string;
  /** The seat of the player who asked for the review (from the game page). */
  seat?: number;
}

export function Replay() {
  const [source, setSource] = useState<Source | null>(null);
  const [message, setMessage] = useState('');
  const { game, topo, error } = useOpenGame(source?.text ?? null);
  const playback = usePlayback(game);
  // Whose review it is: picked on the page, else the seat the game page
  // named, else the first person in the game. A new record starts over.
  const [picked, setPicked] = useState<number | null>(null);
  useEffect(() => setPicked(null), [source]);
  const focus = picked ?? (game ? reviewSeat(game.record.players, source?.seat) : 0);
  const { pos, go, playing, setPlaying, speed, setSpeed } = playback;
  // Monte Carlo in Web Workers, started as soon as a record opens.
  const chances = useWinChances(game ? (source?.text ?? null) : null);
  // The setup review, in its own workers, also from the start.
  const setup = useSetupReview(game ? (source?.text ?? null) : null, focus);
  // Every decision of the player in focus graded, also from the start.
  const grades = useMoveGrades(game ? (source?.text ?? null) : null, focus);
  const marks = useMemo(() => byEvent(grades), [grades]);

  // Opened by the game page ("Review this game"): wait for its record.
  // Otherwise the example opens by itself, so the tab is never empty.
  const [waiting, setWaiting] = useState(() => expectsHandoff(location.hash));
  useEffect(() => {
    const showExample = () =>
      exampleRecord().then(
        (text) => setSource((s) => s ?? { text, label: EXAMPLES[0].label }),
        (e) => setMessage(`Cannot load the example game: ${e.message}`),
      );
    if (!expectsHandoff(location.hash)) {
      void showExample();
      return;
    }
    const stop = receiveFromOpener((h, origin) => {
      setWaiting(false);
      if (h.type === 'catan-review:error') {
        setMessage(`The game page could not send the game: ${h.message}`);
        return;
      }
      const label = h.label ?? `a game from ${new URL(origin).host}`;
      readRecord(h.record).then(
        ({ text }) => {
          setSource({ text, label, seat: h.seat });
          setMessage('');
        },
        (e) => setMessage(`${label}: cannot read it, because ${e instanceof Error ? e.message : String(e)}.`),
      );
    });
    if (!stop) {
      setWaiting(false);
      setMessage('This link waits for a game from the page that opened it, and no page did. Here is the example game instead.');
      void showExample();
      return;
    }
    // The game page answers within a second; after that it was probably closed.
    const late = setTimeout(() => {
      setWaiting((w) => {
        if (w) setMessage('The game page has not sent the game. Is its tab still open? Try "Review this game" again.');
        return false;
      });
    }, 15000);
    return () => {
      stop();
      clearTimeout(late);
    };
  }, []);

  const openFile = async (file: File) => {
    try {
      setSource({ text: (await readRecord(await file.arrayBuffer())).text, label: file.name });
      setMessage('');
    } catch (e) {
      setMessage(`${file.name}: cannot read it, because ${e instanceof Error ? e.message : String(e)}.`);
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

  // The heat map of the setup settlement at this move, if there is one.
  // Else the best move, while a graded move of the player in focus is shown.
  const heat = () => {
    if (!topo || !game) return null;
    const m = setup ? focusMove(setup.moves, pos) : null;
    const review = m === null ? undefined : setup?.reviews[m];
    if (setup && m !== null && review) return <SetupHeat review={review} move={setup.moves[m]} topo={topo} players={game.record.players} />;
    return <BestMoveSpot run={grades} pos={pos} topo={topo} color={PLAYER_COLORS[game.record.players[focus]?.color] ?? '#888'} />;
  };

  const shownError = message || (error && `${source?.label}: ${error}`) || (game?.error && `${source?.label}: the engine stopped early: ${game.error}`);

  return (
    <>
      <div className="page-head">
        <div>
          <h1>Replay</h1>
          <p className="lede">
            Step through a finished game, rebuilt state by state by the engine in WebAssembly: each player's chance to win,
            and every decision of the player under review graded against the best move.
          </p>
        </div>
        <div className="open">
          <label
            className="button primary"
            htmlFor="file"
            title="A .json or .json.gz record that catan-project saves after every game; you can also drop one anywhere on the page"
          >
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
          {game && (
            <label className="focus-pick small muted">
              Review for
              <select value={focus} onChange={(e) => setPicked(Number(e.target.value))} aria-label="Whose review">
                {game.record.players.map((p) => (
                  <option key={p.seat} value={p.seat}>
                    {seatName(game.record.players, p.seat)}
                    {p.kind === 'bot' ? ' (bot)' : ''}
                  </option>
                ))}
              </select>
            </label>
          )}
        </div>
      </div>

      {shownError && (
        <p className="message error" role="status">
          {shownError}
        </p>
      )}

      {game && topo ? (
        <div className="viewer">
          <section className="board-pane">
            <GameBoard game={game} topo={topo} pos={pos} overlay={heat()} />
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
            <WinChart
              run={chances}
              players={game.record.players}
              stateCount={game.stateCount}
              pos={pos}
              focus={focus}
              onSeek={step}
              graded={grades?.seat === focus ? grades.reviews : []}
            />
          </section>
          <aside className="side">
            <Status game={game} pos={pos} />
            <Players state={game.state(pos)} players={game.record.players} />
            <MoveLog game={game} pos={pos} marks={marks} onPick={step} />
          </aside>
          <section className="charts">
            <MoveReviewPanel
              run={grades}
              names={{ players: game.record.players, board: game.board, topo }}
              pos={pos}
              focus={focus}
              onSeek={step}
            />
            <SetupPanel run={setup} players={game.record.players} pos={pos} focus={focus} onSeek={step} />
            <LuckPanel run={chances} players={game.record.players} pos={pos} focus={focus} />
          </section>
        </div>
      ) : waiting && !shownError ? (
        <p className="message" role="status">
          Waiting for the game from the page that opened this one…
        </p>
      ) : (
        !shownError && <div className="board-area placeholder viewer-placeholder" aria-busy="true" />
      )}
    </>
  );
}
