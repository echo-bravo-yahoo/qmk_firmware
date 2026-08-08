# rules.mk conditional ordering

`rules.mk` is a plain Makefile fragment: QMK's build system does not defer or reorder anything in it. An `ifeq` block evaluates immediately, against whatever value its variable holds at that exact line -- not against the value it ends up with after the rest of the file has run.

The practical consequence: if a new build flag needs to force an existing variable to a different value before some other `ifeq` block inspects that variable, the new flag's block must appear **textually before** that `ifeq` in the file. Placing it after looks correct on a read-through (both blocks are right there, clearly related) but does nothing -- the earlier `ifeq` already locked in its decision using the old value.

This bites hardest with mutually-exclusive feature flags, e.g. a new diagnostic/alternate build mode that needs to suppress an existing feature so only one of two conflicting definitions (two `oled_task_user()`s, two `process_record_user()`s, ...) gets compiled in. Wrong order doesn't fail to build the new mode -- it silently builds _both_, which then fails at link time with a duplicate-symbol error that doesn't obviously point back to the ordering.

## Example (from `keyboards/crkbd/keymaps/aeby/rules.mk`)

`KEYLOG_ENABLE=yes` is meant to force `STARMAP_ENABLE = no` so the keymap's two alternate OLED modes stay mutually exclusive. First draft placed the `KEYLOG_ENABLE` block after the existing `STARMAP_ENABLE` block:

```makefile
ifeq ($(strip $(STARMAP_ENABLE)),yes)      # <- runs first, sees STARMAP_ENABLE=yes
    SRC += oled_gfx.c ...
endif

KEYLOG_ENABLE ?= no
ifeq ($(strip $(KEYLOG_ENABLE)),yes)
    STARMAP_ENABLE = no                     # <- too late, already evaluated above
    ...
endif
```

With `KEYLOG_ENABLE=yes` on the command line, this compiles the STARMAP sources _and_ the KEYLOG sources together -- both `oled_task_user()` definitions land in the same translation unit's link.

Fix: move the whole `KEYLOG_ENABLE` block above the `STARMAP_ENABLE` check, so `STARMAP_ENABLE = no` is already in effect by the time that `ifeq` runs.

## Takeaway

When adding a flag that's meant to override another flag, place it earlier in the file than every `ifeq` that reads the overridden variable -- and check the file top-to-bottom, not just "is this near the related code," before trusting that a build actually excludes what it's supposed to exclude.