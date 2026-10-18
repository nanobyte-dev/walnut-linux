#!/usr/bin/env python3
"""Call stacks around a function in analyze.py's pickles (docs/HOTPATHS.md). Shares are % of the game
process's samples.
  stacks.py callers PKL REGEX [DEPTH [TOP]]   the DEPTH frames above the outermost frame matching REGEX
  stacks.py callees PKL REGEX [DEPTH [TOP]]   the DEPTH frames below the innermost one
  stacks.py leaves REGEX PKL...               the sampled function (leaf) of the samples through REGEX
e.g. stacks.py callers sauerbraten.pkl '^nv30_query_result$' 8: who waits for the query results."""
import sys, pickle, re, collections


def load(p):
    d = pickle.load(open(p, 'rb'))
    return d['game'], sum(d['game'].values())


def around(mode, pkl, rx, depth, top):
    game, tot = load(pkl)
    c, hit = collections.Counter(), 0
    for (th, frames), n in game.items():
        idx = [i for i, f in enumerate(frames) if rx.search(f[2] or '')]
        if not idx:
            continue
        hit += n
        if mode == 'callers':
            i = idx[0]
            sel = frames[max(0, i - depth):i + 1]
        else:
            i = idx[-1]
            sel = frames[i:i + depth + 1]
        c[(th,) + tuple(f[2] for f in sel)] += n
    print('%s: %d samples, %.1f%% through %s' % (pkl, tot, 100.0 * hit / tot, rx.pattern))
    for k, n in c.most_common(top):
        print('%5.2f%%  %s' % (100.0 * n / tot, ' > '.join(map(str, k))))


def leaves(rx, pkls):
    for p in pkls:
        game, tot = load(p)
        c, hit = collections.Counter(), 0
        for (th, frames), n in game.items():
            if any(rx.search(f[2] or '') for f in frames):
                hit += n
                c[frames[-1][2]] += n
        print('%s: %.2f%% through %s' % (p, 100.0 * hit / tot, rx.pattern))
        for k, n in c.most_common(14):
            print('  %5.2f%% %s' % (100.0 * n / tot, k))


def main(a):
    if len(a) >= 3 and a[0] in ('callers', 'callees'):
        around(a[0], a[1], re.compile(a[2]), int(a[3]) if len(a) > 3 else 6, int(a[4]) if len(a) > 4 else 20)
    elif len(a) >= 3 and a[0] == 'leaves':
        leaves(re.compile(a[1]), a[2:])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
