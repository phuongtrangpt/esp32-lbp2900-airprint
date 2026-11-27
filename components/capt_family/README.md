# Isolated CAPT family protocol

GPL-3.0-or-later; preserve the notices in vendor files and COPYING.

Command/status/model operations are adapted from mounaiban/captdriver commit
62719249ac34633338be54bc74beddd0e7003d38, originally by Alexey Galakhov and
Alexei Gordeev. No CUPS constructor registration is retained. Only the 3010 and
6000 operation tables are exported. The 6200 prototype explicitly assumes the
6000 operation table and has not been hardware validated.

The compressor, portability header and bounded transport are private copies of
the existing repository's CAPT fork at 3a9b72b4bbcb6d18a941660ccf9ab162d539ab4a.
Symbols are prefixed cf_ so none can interpose on the stable LBP2900 component.

See ../../docs/CAPT_FAMILY.md for provenance, deliberate port changes, limits
and verification instructions.
