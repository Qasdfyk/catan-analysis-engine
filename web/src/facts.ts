// The numbers the page quotes, in one place. Each comes from a measurement
// written up in docs/ (named next to it); change them together.

/** docs/performance.md, "Results": games per second on one core, 4 players. */
export const SPEED = [
  { name: 'catan-project game.py', short: 'game.py', gamesPerSecond: 2.4, note: 'Python, the reference rules' },
  { name: 'Catanatron 3.2.1', short: 'Catanatron', gamesPerSecond: 27.9, note: 'Python, a popular open-source engine' },
  { name: 'This engine, native', short: 'Native C++', gamesPerSecond: 10_800, note: 'GCC 15 -O3, i7-11800H' },
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
 * The arena (docs/arena.md): one candidate against three copies of an
 * opponent, plain deals, catan_arena --deals N --threads 12 (seed 1). `half`
 * is the half-width of the 95% interval. An even share is 25%. Only the
 * matches the demo shows; the full table is in the doc.
 */
export const ARENA = {
  matches: [
    { candidate: 'hard', opponents: 'hard', win: 0.245, half: 0.009, games: 8000, note: 'the control: an equal player wins 25%' },
    { candidate: 'hard', opponents: 'Catanatron AlphaBeta', win: 0.52, half: 0.069, games: 200, note: 'no trades between players' },
    { candidate: 'ISMCTS, 400 playouts', opponents: 'hard', win: 0.458, half: 0.04, games: 600, note: 'the hard bot plus search; sees only what its seat sees' },
    { candidate: 'ISMCTS, 400 playouts', opponents: 'Catanatron AlphaBeta', win: 0.66, half: 0.093, games: 100, note: 'no trades between players' },
  ],
} as const;
