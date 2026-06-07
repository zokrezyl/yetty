% Ode to Joy (Beethoven, 9th Symphony) — melody in D major.
% A yless/ymusic demo: enough measures to wrap into several systems and
% scroll vertically. Exercises clef, key signature, time signature, octave
% changes, dotted rhythms and eighth notes.
\version "2.24.0"
\relative c'' {
  \clef treble
  \key d \major
  \time 4/4

  fis4 fis g a    | a g fis e      | d d e fis      | fis4. e8 e2    |
  fis4 fis g a    | a g fis e      | d d e fis      | e4. d8 d2      |
  e4 e fis d      | e fis8 g fis4 d | e fis8 g fis4 e | d e a,2       |
  fis'4 fis g a   | a g fis e      | d d e fis      | e4. d8 d2      |
}
