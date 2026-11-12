# Interface texts

How the texts of the interface are written: labels, buttons, menus, hints, messages and errors. Every new or changed text follows these rules.

<!-- shared-start: the rules up to shared-end are the same in every project; change them only where all projects change together. -->

## Rules

| Rule | Write | Not |
| --- | --- | --- |
| English only, with American spelling. | Color, Center, Analyzing colors | Colour, Centre, Analysing colours |
| Sentence case for labels, buttons, menu items, tab names and headings. | Save current settings, Restore defaults | Save Current Settings |
| The ellipsis character `…`, never three dots, and only on a command that asks for more before it acts, and on a progress text. A command that acts at once has none. | Open file…, Export…, Loading…, Downloading… | Loading..., Restore defaults… |
| Hints and descriptions are full sentences ending with a period. Labels have no period. | This item has no settings. (hint); Lock aspect ratio (label) | Small = quiet, large = loud (hint) |
| Clauses are joined with a colon. The en dash `–` is only for ranges and as a separator in names and labels. | The file could not be read: it is damaged. 22–119 mm, Inside – left | Drop a file – it is added to the list |
| Curly quotes “ ” and the apostrophe ’. | Save changes to “Draft”?, Don’t save | Save changes to "Draft"?, Don't save |
| No "Please". | Choose PNG or JPEG. | Please choose PNG or JPEG. |
| A count is written as "Label: n". | Files chosen: 3, Errors: 2 | 3 files chosen, Errors (2) |
| A control with a shortcut shows it as "Action (shortcut)" in its tooltip, with the keys of the system it runs on. | Undo (Ctrl+Z), New tab (⌘T) | Ctrl+Z: Undo |
| An error says what happened, then what to do, in the user's terms, never as a stack trace. | The app’s window was not found: is the app running? | Window handle is null. |
| One name per concept: the terms in `docs/architecture.md`. | the project’s own terms | synonyms for them |
| Units after a space, degrees with no space, × between dimensions. | 10.4 mm, 120 × 120 mm, 360°, 80 MB | 10.4mm, 120x120 mm, 360 deg |

Texts take values through the language's string formatting (template literals, f-strings, `std::format`), never through concatenated strings.

<!-- shared-end -->

## This project's own rules

- A command's title and description (`CommandSpec::title` and `description`) are interface texts: menus, the command palette, tooltips and the `/v1` API show them. A title follows the rules for menu items, and a description those for hints.
- Messages that the local API and the AI service return in an error's `message` reach the user through the app, so they follow the rules for errors too.
- Names of fields and commands in developer messages take curly quotes too: The answer has no “jobId”. Code inside a message keeps straight quotes: JSON such as `"type": "object"` and examples of what to type.
