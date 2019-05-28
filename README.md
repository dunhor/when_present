# when_present
Given a source file and line number, identifies which preprocessor conditional(s) must be true or false for the line to be included in compilation

## Usage
`when_present` accepts a single file path and one or more line numbers. E.g. to specify a single line number:
```cmd
when_present --file foo.h --lines 3
```
Or multiple line numbers:
```cmd
when_present --file foo.h --lines 3 8 42
```

## Limitations
There are some obvious limitations here. For one, `#include`s are not followed. E.g. if you are looking at the conditions for a line in `bar.h`, but your code is including `foo.h` which conditionally includes `bar.h`, then this will not capture the conditions from `foo.h`. For such a scenario, you would need to run this executable twice: once for `bar.h` for the line number(s) that you care about and again for `foo.h` for the line numbers that `#include "bar.h"`.

Another limitation - or perhaps better phrased as a missing feature - is that macros are not expanded and dependencies are not simplified. For example, consider the following:
```c++
#define FOO
#if LIBRARY_VERSION > 8
#define BAR 42
#endif
#if defined(FOO) && defined(BAR)
void CoolFunction(); // Line 42
#endif
```
If you were to run `when_present` for line 42 on the above, it would only say that `#if defined(FOO) && defined(BAR)` must be `true`. That is, even though `FOO` is unconditionally defined and even though `BAR`'s definition depends on the value of `LIBRARY_VERSION`, these dependencies are not explored.
