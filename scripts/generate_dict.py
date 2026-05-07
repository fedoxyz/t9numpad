#!/usr/bin/env python3
"""
scripts/generate_dict.py
Generate a t9numpad word-frequency dictionary from a plain text corpus.

Usage:
    python3 generate_dict.py corpus.txt > data/en.dict
    python3 generate_dict.py --min-freq 5 corpus.txt > data/en.dict
    python3 generate_dict.py --wordlist /usr/share/dict/words > data/en.dict
"""

import argparse
import re
import sys
from collections import Counter
from pathlib import Path


def load_corpus(path: Path) -> Counter:
    words: Counter = Counter()
    with path.open(encoding="utf-8", errors="ignore") as f:
        for line in f:
            for word in re.findall(r"[a-zA-Z']+", line):
                words[word.lower()] += 1
    return words


def load_wordlist(path: Path) -> Counter:
    """Assign uniform frequency of 1 to each word (no corpus needed)."""
    words: Counter = Counter()
    with path.open(encoding="utf-8", errors="ignore") as f:
        for line in f:
            word = line.strip().lower()
            if re.fullmatch(r"[a-z']+", word):
                words[word] = 1
    return words


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate t9numpad dictionary")
    parser.add_argument("input", nargs="?", help="Corpus file or word list")
    parser.add_argument("--wordlist", action="store_true",
                        help="Treat input as a word-per-line list (no frequencies)")
    parser.add_argument("--min-freq", type=int, default=2,
                        help="Minimum frequency to include (default: 2)")
    parser.add_argument("--max-words", type=int, default=100_000,
                        help="Maximum words to output (default: 100000)")
    args = parser.parse_args()

    if args.input is None:
        parser.print_help()
        sys.exit(1)

    path = Path(args.input)
    if not path.exists():
        print(f"error: file not found: {path}", file=sys.stderr)
        sys.exit(1)

    if args.wordlist:
        words = load_wordlist(path)
    else:
        words = load_corpus(path)

    filtered = {w: f for w, f in words.items()
                if f >= args.min_freq and len(w) >= 2}

    top = sorted(filtered.items(), key=lambda x: -x[1])[:args.max_words]

    for word, freq in top:
        print(f"{word} {freq}")

    print(f"# Wrote {len(top)} words", file=sys.stderr)


if __name__ == "__main__":
    main()
