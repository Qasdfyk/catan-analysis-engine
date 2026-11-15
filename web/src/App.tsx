// The page frame: header with the tabs, the tab named in the URL, footer.
// Tabs are hash routes (#/replay): GitHub Pages serves one file and cannot
// send /replay back to it, but a hash never reaches the server.

import { PLATFORM_URL, REPO_URL } from './facts';
import { useHashTab } from './hooks';
import type { Tab } from './nav';
import { HowItWorks } from './pages/HowItWorks';
import { Performance } from './pages/Performance';
import { Replay } from './pages/Replay';
import { Start } from './pages/Start';

const NAV: { tab: Tab; label: string }[] = [
  { tab: 'start', label: 'Start' },
  { tab: 'replay', label: 'Replay' },
  { tab: 'performance', label: 'Performance' },
  { tab: 'how-it-works', label: 'How it works' },
];

export function App() {
  const tab = useHashTab();
  return (
    <>
      <header className="top">
        <a className="brand" href="#/">
          <span className="brand-mark" aria-hidden="true">
            ◆
          </span>{' '}
          catan-analysis-engine
        </a>
        <nav>
          {NAV.map((n) => (
            <a key={n.tab} href={`#/${n.tab === 'start' ? '' : n.tab}`} aria-current={tab === n.tab ? 'page' : undefined}>
              {n.label}
            </a>
          ))}
          <a className="nav-gh" href={REPO_URL} aria-label="Source on GitHub">
            GitHub ↗
          </a>
        </nav>
      </header>
      <main className="page" key={tab}>
        {tab === 'start' && <Start />}
        {tab === 'replay' && <Replay />}
        {tab === 'performance' && <Performance />}
        {tab === 'how-it-works' && <HowItWorks />}
      </main>
      <footer>
        <span>
          <a href={REPO_URL}>catan-analysis-engine</a> · game records from <a href={PLATFORM_URL}>catan-project</a>
        </span>
        <span className="muted">Everything on this page runs in your browser.</span>
      </footer>
    </>
  );
}
