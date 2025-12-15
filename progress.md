# Progress Notes

## What changed
- Added a Basic/Advanced view toggle to the settings window so we can switch between simpler and full layouts without rearranging existing controls.
- Introduced a shared `SettingDetail` flag and visibility helpers so any setting control can be marked as Basic or Advanced when it is added.
- Wired our option helper widgets to respect the visibility flag, making it easy to hide Advanced-only items while keeping layout spacing intact.

## How to use
- Use the new "View" radio buttons at the top of the Settings window to switch between Basic and Advanced modes.
- When adding a new setting control, pass `SettingDetail::Advanced` to the option helper (e.g. `OptionCheckbox(..., SettingDetail::Advanced)`) to make it Advanced-only. Leaving it at the default keeps it visible in both modes.
- Controls keep their declared order; Advanced-only entries simply collapse in Basic view so spacing stays tight without changing the sequence. You can insert a Basic or Advanced control anywhere in an existing layout and it will appear in that exact spot when visible.

## Notes for follow-up
- The Advanced and Debug tabs are hidden while in Basic mode; additional settings can be marked Advanced as needed to simplify other tabs.
