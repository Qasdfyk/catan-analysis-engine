// The viewer page: open a record, step through it.
//
// All game logic is in the engine (WebAssembly) and the board is
// catan-project's own (BoardView.tsx). This file keeps the position (which
// state is shown) and draws the rest of that state: players, move list.

import { PLAYER_COLORS } from '@platform/utils/labels';
import { renderBoard } from './BoardView';
import { OpenGame, loadEngine, readRecordBytes, topology } from './engine';
import { eventText, isTradeTalk } from './events';
import { DEV_CARDS, RESOURCES, type GameState, type RecordPlayer, type Topology } from './types';
import { toGameView, toPlayers } from './view';
import './style.css';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

let topo: Topology | null = null;
let game: OpenGame | null = null;
/** The state shown: 0 before the first event, i after event i - 1. */
let pos = 0;

function showMessage(text: string, isError = false): void {
  const el = $('message');
  el.textContent = text;
  el.classList.toggle('error', isError);
  el.hidden = text === '';
}

async function open(text: string, label: string): Promise<void> {
  try {
    const engine = await loadEngine();
    topo ??= await topology();
    const next = new OpenGame(engine, text);
    if (next.stateCount === 0) {
      next.close();
      showMessage(`${label}: not a game record the engine can read (${next.error}).`, true);
      return;
    }
    game?.close();
    game = next;
    // A refused event does not hide the game: show how far it got.
    showMessage(game.error ? `${label}: the engine stopped early: ${game.error}` : '', game.error !== '');
    $('viewer').hidden = false;
    const slider = $<HTMLInputElement>('slider');
    slider.max = String(game.stateCount - 1);
    renderLog();
    go(0);
  } catch (e) {
    showMessage(`${label}: ${e instanceof Error ? e.message : String(e)}`, true);
  }
}

async function openFile(file: File): Promise<void> {
  try {
    await open(await readRecordBytes(await file.arrayBuffer()), file.name);
  } catch (e) {
    showMessage(`${file.name}: cannot read it (${e instanceof Error ? e.message : String(e)}).`, true);
  }
}

function go(to: number): void {
  if (!game) return;
  pos = Math.max(0, Math.min(game.stateCount - 1, to));
  render();
}

/** The next (or previous) state right after a dice roll: the start of a turn. */
function turnStart(direction: 1 | -1): number {
  if (!game) return 0;
  const events = game.record.events;
  for (let i = pos + direction; i > 0 && i < game.stateCount; i += direction) {
    if (events[i - 1].action === 'roll_dice') return i;
  }
  return direction === 1 ? game.stateCount - 1 : 0;
}

function playerName(players: RecordPlayer[], seat: number | null): string {
  return seat === null ? '' : (players[seat]?.name ?? `seat ${seat}`);
}

function chip(seat: number, players: RecordPlayer[]): string {
  const color = PLAYER_COLORS[players[seat]?.color] ?? '#888';
  return `<span class="chip" style="background:${color}"></span>`;
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}

function render(): void {
  if (!game || !topo) return;
  const state = game.state(pos);
  const before = pos > 0 ? game.state(pos - 1) : null;
  const players = game.record.players;
  const event = pos > 0 ? game.record.events[pos - 1] : null;

  renderBoard($('board'), {
    view: toGameView(state, game.record, topo),
    before: before ? toGameView(before, game.record, topo) : null,
    players: toPlayers(game.record, state),
    rolled: event?.action === 'roll_dice',
  });
  $<HTMLInputElement>('slider').value = String(pos);
  $('position').textContent = `Move ${pos} of ${game.stateCount - 1} · turn ${state.turn}`;
  renderStatus(state, players, event ? `${playerName(players, event.seat)} ${eventText(event, players)}` : 'Start of the game');
  renderPlayers(state, players);

  const log = $('log');
  log.querySelector('.current')?.classList.remove('current');
  const current = log.querySelector(`[data-i="${pos - 1}"]`);
  if (current) {
    current.classList.add('current');
    current.scrollIntoView({ block: 'nearest' });
  }
}

function renderStatus(state: GameState, players: RecordPlayer[], last: string): void {
  const dice = state.dice ? `<span class="dice">${state.dice[0]}</span><span class="dice">${state.dice[1]}</span>` : '';
  const result = state.game_over
    ? `<strong>${escapeHtml(playerName(players, state.winner))} wins.</strong>`
    : `${chip(state.current, players)} ${escapeHtml(playerName(players, state.current))} to move · ${state.phase.replace(/_/g, ' ')}`;
  $('status').innerHTML = `
    <div class="status-line">${result} ${dice}</div>
    <div class="last">${escapeHtml(last)}</div>
    <div class="bank">Bank: ${RESOURCES.map((r, i) => `<span class="res ${r}" title="${r}">${state.bank[i]}</span>`).join('')}</div>`;
}

function renderPlayers(state: GameState, players: RecordPlayer[]): void {
  $('players').innerHTML = state.players
    .map((p, seat) => {
      const info = players[seat];
      const cards = RESOURCES.map(
        (r, i) => `<span class="res ${r}${p.resources[i] === 0 ? ' none' : ''}" title="${r}">${p.resources[i]}</span>`,
      ).join('');
      const dev = DEV_CARDS.flatMap((c, i) => {
        const n = p.dev_cards[i] + p.new_dev_cards[i];
        return n > 0 ? [`${n}× ${c.replace(/_/g, ' ')}`] : [];
      }).join(', ');
      const badges = [
        state.winner === seat ? '<span class="badge win">winner</span>' : '',
        state.longest_road === seat ? '<span class="badge">longest road</span>' : '',
        state.largest_army === seat ? '<span class="badge">largest army</span>' : '',
      ].join('');
      const hidden = p.hidden_points > 0 ? ` <span class="hidden-points" title="victory point cards">+${p.hidden_points}</span>` : '';
      const level = info?.kind === 'bot' ? ` <span class="muted">bot ${info.bot_level ?? ''}</span>` : '';
      return `
        <div class="panel player${state.current === seat && !state.game_over ? ' to-move' : ''}">
          <div class="player-head">${chip(seat, players)}<strong>${escapeHtml(info?.name ?? `seat ${seat}`)}</strong>${level}
            <span class="points">${p.points}${hidden}</span></div>
          <div class="cards">${cards}</div>
          <div class="muted small">road ${state.road_lengths[seat]} · knights ${p.knights}${dev ? ` · ${dev}` : ''}</div>
          <div>${badges}</div>
        </div>`;
    })
    .join('');
}

function renderLog(): void {
  if (!game) return;
  const players = game.record.players;
  let turn = -1;
  const items: string[] = [];
  game.record.events.forEach((e, i) => {
    // A heading above the first event of every turn after the setup.
    if (e.action === 'roll_dice' && e.turn !== turn) {
      turn = e.turn;
      items.push(`<li class="turn">Turn ${e.turn}</li>`);
    }
    const talk = isTradeTalk(e) ? ' talk' : '';
    items.push(
      `<li data-i="${i}" class="event${talk}">${chip(e.seat, players)}<b>${escapeHtml(playerName(players, e.seat))}</b> ${escapeHtml(eventText(e, players))}</li>`,
    );
  });
  $('log').innerHTML = items.join('');
}

// --- Wiring ---

$('first').onclick = () => go(0);
$('last').onclick = () => go(Infinity);
$('prev').onclick = () => go(pos - 1);
$('next').onclick = () => go(pos + 1);
$('prev-turn').onclick = () => go(turnStart(-1));
$('next-turn').onclick = () => go(turnStart(1));
$<HTMLInputElement>('slider').oninput = (e) => go(Number((e.target as HTMLInputElement).value));
$('log').onclick = (e) => {
  const li = (e.target as HTMLElement).closest<HTMLElement>('[data-i]');
  if (li) go(Number(li.dataset.i) + 1);
};
$<HTMLInputElement>('trade-talk').onchange = (e) =>
  $('log').classList.toggle('hide-talk', !(e.target as HTMLInputElement).checked);

document.addEventListener('keydown', (e) => {
  if (!game || e.target instanceof HTMLInputElement && e.target.type !== 'range') return;
  const keys: Record<string, () => number> = {
    ArrowLeft: () => pos - 1,
    ArrowRight: () => pos + 1,
    PageUp: () => turnStart(-1),
    PageDown: () => turnStart(1),
    Home: () => 0,
    End: () => Infinity,
  };
  const to = keys[e.key];
  if (to) {
    e.preventDefault();
    go(to());
  }
});

$<HTMLInputElement>('file').onchange = (e) => {
  const file = (e.target as HTMLInputElement).files?.[0];
  if (file) void openFile(file);
};
$('example').onclick = async () => {
  const response = await fetch(new URL('example.json.gz', document.baseURI));
  await open(await readRecordBytes(await response.arrayBuffer()), 'example game');
};
document.addEventListener('dragover', (e) => e.preventDefault());
document.addEventListener('drop', (e) => {
  e.preventDefault();
  const file = e.dataTransfer?.files[0];
  if (file) void openFile(file);
});

$('speed-run').onclick = () => {
  const games = Math.max(100, Number($<HTMLInputElement>('speed-games').value) || 20000);
  const out = $('speed-out');
  const button = $<HTMLButtonElement>('speed-run');
  button.disabled = true;
  out.textContent = `Playing ${games} games…`;
  const worker = new Worker(new URL('./bench-worker.ts', import.meta.url), { type: 'module' });
  worker.onmessage = (msg) => {
    const r = msg.data as { games: number; actions: number; refused: number; checksum: string; seconds: number };
    out.textContent =
      `${r.games} games, 4 players, seed 1, 1 thread\n` +
      `  ${(r.actions / r.games).toFixed(1)} actions per game, ${r.refused} refused actions\n` +
      `  checksum ${r.checksum}\n` +
      `time ${r.seconds.toFixed(3)} s: ${Math.round(r.games / r.seconds)} games/s, ` +
      `${(r.actions / r.seconds / 1e6).toFixed(2)} M actions/s, ${((r.seconds / r.games) * 1e6).toFixed(1)} us per game`;
    button.disabled = false;
    worker.terminate();
  };
  worker.onerror = (err) => {
    out.textContent = `The worker failed: ${err.message}`;
    button.disabled = false;
  };
  worker.postMessage({ games });
};

// Start compiling the module right away; the first click then finds it ready.
void loadEngine().catch((e) => showMessage(`Cannot load the engine: ${e}`, true));
