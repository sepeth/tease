# tease

This is a CLI tool similar to `chronic` in
[moreutils](https://joeyh.name/code/moreutils/). chronic suppresses the output
completely and only prints the full output _at the end_ and _if the program fails_.
`tease` on the other hand, teases you with some output, the last line printed,
as the program progresses. And similarly, it only prints the full output on failure.

With chronic, it is hard to see if a long running program is making a progress.
You can think of `tease` as `tail -f -n 1` but the same last line gets
overriden.

This is useful for verbose programs that usually succeeds, and you only care
about the full output if the program fails, such as build tools.
