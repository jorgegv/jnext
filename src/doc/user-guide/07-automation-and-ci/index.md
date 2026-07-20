# 7. Automation and CI

JNEXT tests itself by running programs headless, capturing screenshots at
fixed points and comparing them against checked-in references. **The same
machinery works on your program.** Nothing here is a private test hook: every
flag used by JNEXT's own suite is a documented command-line option.

This chapter shows how to build that loop for your own software. The worked
example at the end is lifted from JNEXT's suite in `test/00regression/`, so
you can read the real thing alongside it.
