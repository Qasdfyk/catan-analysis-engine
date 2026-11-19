// The numbers the page quotes, in one place. Each comes from a measurement
// written up in docs/ (named next to it); change them together.

/** docs/performance.md, "Results": games per second on one core, 4 players. */
export const SPEED = [
  { name: 'catan-project game.py', short: 'game.py', gamesPerSecond: 2.4, note: 'Python, the reference rules' },
  { name: 'Catanatron 3.2.1', short: 'Catanatron', gamesPerSecond: 27.9, note: 'Python, a popular open-source engine' },
  { name: 'This engine, native', short: 'Native C++', gamesPerSecond: 10_800, note: 'GCC 15 -O3, i7-11800H' },
] as const;

/** docs/performance.md, "Threads": native games per second by thread count. */
export const THREADS = [
  { threads: 1, gamesPerSecond: 10_600 },
  { threads: 2, gamesPerSecond: 18_700 },
  { threads: 4, gamesPerSecond: 31_200 },
  { threads: 8, gamesPerSecond: 45_600 },
  { threads: 16, gamesPerSecond: 50_600 },
] as const;

/** docs/performance.md, the profile before and after B8's two fixes. */
export const PROFILE = {
  before: { gamesPerSecond: 4_120, longestRoadShare: 0.45 },
  after: { gamesPerSecond: 10_800 },
} as const;

/** docs/difftest.md: lines of game.py the difference test runs, planted bugs caught. */
export const CORRECTNESS = {
  coverage: 0.93,
  mutantsCaught: 12,
  mutants: 12,
  /** Planted bugs caught as the test grew. */
  mutationRuns: [
    { label: 'First version of the difference test', caught: 9 },
    { label: '+ bank squeeze, aimed probes, steal probes', caught: 11 },
    { label: '+ the C++ unit tests', caught: 12 },
  ],
} as const;

/** docs/performance.md, "Data layout": one Game, and what copying it costs. */
export const GAME_LAYOUT = { bytes: 768, cacheLine: 64, copyNs: 21 } as const;

/**
 * Checksums the native build prints for `catan_selfplay --games N --seed 1`
 * (any thread count). The in-browser benchmark plays the same games, so its
 * checksum must be equal.
 */
export const NATIVE_CHECKSUMS: Record<number, string> = {
  5_000: '98544073',
  20_000: '394587554',
  100_000: '1972913071',
};

/**
 * The same for `catan_selfplay --games N --seed 1 --bots zoo`: zoo bots, the
 * C++ port of catan-project's Go bots (docs/zoo.md).
 */
export const NATIVE_ZOO_CHECKSUMS: Record<number, string> = {
  500: '8137758',
  2_000: '32562187',
  10_000: '162606903',
};

/** Per action, how much faster than game.py (docs/performance.md). */
export const SPEEDUP_VS_PYTHON = 4_000;

export const REPO_URL = 'https://github.com/Qasdfyk/catan-analysis-engine';
export const PLATFORM_URL = 'https://github.com/Qasdfyk/catan-project';

/**
 * catan-project docs/bot-zoo.md, "Results": 400 games of four zoo bots
 * (tournament seed 1), statistics from app.services.record_stats. `win` is
 * the share of seats of the archetype that won; an even share is 25%.
 */
export const ZOO = {
  games: 400,
  archetypes: [
    { name: 'ore-wheat', note: 'cities and development cards', win: 0.30, setupOreWheat: 0.536, setupWoodBrick: 0.253, cities: 2.51, settlements: 0.73, roads: 4.8, devCards: 5.3 },
    { name: 'wood-brick', note: 'settlements and the Longest Road', win: 0.236, setupOreWheat: 0.272, setupWoodBrick: 0.554, cities: 1.46, settlements: 3.19, roads: 8.1, devCards: 1.1 },
    { name: 'port', note: 'one resource and its port', win: 0.191, setupOreWheat: 0.387, setupWoodBrick: 0.388, cities: 2.04, settlements: 1.58, roads: 5.8, devCards: 2.6 },
    { name: 'balanced', note: 'the hard bot itself', win: 0.276, setupOreWheat: 0.409, setupWoodBrick: 0.392, cities: 2.29, settlements: 1.82, roads: 6.7, devCards: 2.6 },
  ],
  /** Row finished above column, at tables where both sat. */
  headToHead: {
    'ore-wheat': { 'wood-brick': 0.575, port: 0.644, balanced: 0.58 },
    'wood-brick': { 'ore-wheat': 0.425, port: 0.538, balanced: 0.441 },
    port: { 'ore-wheat': 0.356, 'wood-brick': 0.462, balanced: 0.357 },
    balanced: { 'ore-wheat': 0.42, 'wood-brick': 0.559, port: 0.643 },
  },
} as const;

/**
 * The arena (docs/arena.md): one candidate against three copies of an
 * opponent, plain deals, catan_arena --deals N --threads 12 (seed 1). `half`
 * is the half-width of the 95% interval. An even share is 25%.
 */
export const ARENA = {
  matches: [
    { candidate: 'hard', opponents: 'hard', win: 0.245, half: 0.009, games: 8000, note: 'the control: an equal player wins 25%' },
    { candidate: 'hard', opponents: 'random zoo bots', win: 0.37, half: 0.011, games: 8000, note: '' },
    { candidate: 'zoo ore-wheat', opponents: 'hard', win: 0.274, half: 0.01, games: 8000, note: '' },
    { candidate: 'zoo wood-brick', opponents: 'hard', win: 0.199, half: 0.009, games: 8000, note: '' },
    { candidate: 'zoo port', opponents: 'hard', win: 0.152, half: 0.008, games: 8000, note: '' },
    { candidate: 'zoo balanced, never trades', opponents: 'hard', win: 0.176, half: 0.008, games: 8000, note: 'trading is worth a lot' },
    { candidate: 'Catanatron AlphaBeta', opponents: 'hard', win: 0.065, half: 0.024, games: 400, note: 'no trades between players; Catanatron sees every hand' },
    { candidate: 'hard', opponents: 'Catanatron AlphaBeta', win: 0.52, half: 0.069, games: 200, note: 'no trades between players' },
    { candidate: 'ISMCTS, 400 playouts', opponents: 'hard', win: 0.458, half: 0.04, games: 600, note: 'the hard bot plus search; sees only what its seat sees' },
    { candidate: 'ISMCTS, 400 playouts', opponents: 'random elites', win: 0.448, half: 0.049, games: 400, note: '' },
    { candidate: 'ISMCTS, 400 playouts', opponents: 'Catanatron AlphaBeta', win: 0.66, half: 0.093, games: 100, note: 'no trades between players' },
  ],
  /**
   * Set A: 4000 positions from 1000 native zoo games (seed 1001), scored by
   * catan_validate eval. Brier score, lower is better.
   */
  brier: [
    { name: 'Monte Carlo, the game’s own bots', value: 0.1263, lo: 0.1212, hi: 0.1312 },
    { name: 'Monte Carlo, random zoo bots', value: 0.1301, lo: 0.1255, hi: 0.1348 },
    { name: 'baseline model (8 numbers)', value: 0.1497, lo: 0.1456, hi: 0.1541 },
    { name: 'always 1 in 4', value: 0.1875, lo: 0.1875, hi: 0.1875 },
  ],
  /** catan_validate sanity --bot hard --games 200 --seed 3001 */
  sanity: [
    { name: 'win now with a settlement', passed: 137, tried: 137 },
    { name: 'win now with a city', passed: 199, tried: 199 },
    { name: 'win now with Largest Army', passed: 192, tried: 192 },
    { name: 'refuse the leader’s trade', passed: 148, tried: 148 },
    { name: 'rob the leader', passed: 147, tried: 147 },
    { name: 'robber next to the leader', passed: 132, tried: 148 },
    { name: 'first settlement on a rich corner', passed: 181, tried: 200 },
  ],
} as const;

/**
 * The searching bot on one decision of the example game (catan_mcts
 * --decisions 8 --decision 3 --iterations 250 --workers W): playouts per
 * second natively, and the checksum the browser must match for W workers.
 */
export const MCTS_SPEED = {
  /** --iterations 2000 per worker, i7-11800H. */
  threads: [
    { workers: 1, playoutsPerSecond: 3_720 },
    { workers: 2, playoutsPerSecond: 7_320 },
    { workers: 4, playoutsPerSecond: 13_600 },
    { workers: 8, playoutsPerSecond: 18_000 },
    { workers: 16, playoutsPerSecond: 23_700 },
  ] as { workers: number; playoutsPerSecond: number }[],
  /** --iterations 250 --workers W: what the browser's W workers must match. */
  checksums: {
    1: '7708012',
    2: '15528196',
    3: '22959948',
    4: '30351844',
    5: '37743836',
    6: '47800848',
    7: '54112432',
    8: '64041912',
    9: '71053844',
    10: '77613476',
    11: '87546912',
    12: '95435100',
    13: '100865808',
    14: '109034752',
    15: '116222568',
    16: '124138696',
    17: '132739096',
    18: '140687420',
    19: '148239536',
    20: '155528012',
    21: '160450248',
    22: '165364788',
    23: '173497440',
    24: '182558740',
    25: '190674776',
    26: '195401280',
    27: '203957976',
    28: '210349304',
    29: '220646864',
    30: '226634044',
    31: '235399304',
    32: '240822224',
  } as Record<number, string>,
};
