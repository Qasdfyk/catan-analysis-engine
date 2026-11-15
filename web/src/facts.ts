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

/** Per action, how much faster than game.py (docs/performance.md). */
export const SPEEDUP_VS_PYTHON = 4_000;

export const REPO_URL = 'https://github.com/Qasdfyk/catan-analysis-engine';
export const PLATFORM_URL = 'https://github.com/Qasdfyk/catan-project';
