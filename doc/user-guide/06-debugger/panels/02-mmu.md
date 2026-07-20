# MMU

Directly under the CPU registers, and the fastest way to answer "what is
actually mapped where right now".

**Next MMU Mappings** lists all eight 8K slots with the physical page number in
use and the slot type — `ROM` in orange, or `B`*n* naming the 16K bank the page
belongs to. The page shown is the one really in effect, whether it came from
NextREG `0x50`–`0x57` or from legacy 128K paging.

**128K Bank Mappings** shows the port `0x7FFD` state: the selected RAM `Bank`,
the `ROM` select bit, and whether paging is `Lock`ed (red when it is — a locked
`0x7FFD` is a classic cause of "my bank switch does nothing").
