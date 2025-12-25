- Add keyboard bindings to loop mode.
-- We should register keyboard hooks at the beginning.
-- during the loop we have to check if the shortcuts are pressed.
-- we will start by the following shortcuts for moving between windows:
Win + shift + H
Win + shift + J
Win + shift + K
Win + shift + L

These will indicate vim like short cuts for moving around.
When one of the shortcuts is pressed we need to understand the direction, then find the cell in that direction, get the window attached to that cell, make it the foreground window and move the mouse in the middle of it.

See winapi and multi_cell files for already implemented functionality.

- shortcuts for

- refactor loop mode and loop test mode.

-

# Bugs

- New apps dong split the selected window.

- Still get busy arrow when moving with keyboard sometimes.