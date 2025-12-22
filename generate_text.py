#!/usr/bin/env python3
import argparse
import random

def main():
    parser = argparse.ArgumentParser(description='Generate random text from dictionary')
    parser.add_argument('--words-per-line', '-w', type=int, default=10,
                        help='Average words per line (default: 10)')
    parser.add_argument('--lines', '-l', type=int, default=50,
                        help='Number of lines (default: 50)')
    parser.add_argument('--dict', '-d', type=str, default='/usr/share/dict/words',
                        help='Dictionary file path')
    args = parser.parse_args()

    # Load words
    with open(args.dict, 'r') as f:
        words = [line.strip() for line in f if line.strip() and line[0].islower()]

    # Generate lines
    for _ in range(args.lines):
        # Vary words per line slightly for natural feel
        num_words = max(1, args.words_per_line + random.randint(-3, 3))
        line_words = random.choices(words, k=num_words)
        print(' '.join(line_words))

if __name__ == '__main__':
    main()
