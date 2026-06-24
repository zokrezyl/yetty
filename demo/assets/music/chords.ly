% A short chord progression — exercises stacked noteheads (<c e g>),
% accidentals (sharps/flats), rests and dotted rhythms in G major.
\version "2.24.0"
\relative c' {
  \clef treble
  \key g \major
  \time 4/4

  <g b d>2 <c e g>     | <d fis a>2 <g b d>    |
  <e g b>4 r <a cis e>4 r | <d, fis a>4. <g b d>8 <g b d>2 |
}
