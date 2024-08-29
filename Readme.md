# tease

This is a CLI tool similar to `chronic` in
[moreutils](https://joeyh.name/code/moreutils/). `chronic` suppresses the
output completely and only prints the full output at the end if the program
fails. `tease` on the other hand, teases you with some output, the last line
printed, as the program progresses. With `chronic` it is hard to see if a long
running program is making a progress. You can think of tease as `tail -f -n 1`
but the same last line gets overriden. This is useful for verbose programs that
usually succeeds, and you only care about the full output if the prgogram
fails, such as build tools.
