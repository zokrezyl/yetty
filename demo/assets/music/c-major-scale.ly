% C major scale, up and down — the simplest engraving smoke test.
% Exercises treble clef, 4/4 time, quarter notes and a held final note,
% with octave marks taking the line up an octave and back.
\version "2.24.0"
\relative c' {
  \clef treble
  \key c \major
  \time 4/4

  c4 d e f    | g a b c     | c b a g     | f e d c     |
  c2 c        |
}
