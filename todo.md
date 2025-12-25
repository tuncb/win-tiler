# new

- hardening especially for loop.cpp

- code reviews for each system

- Repurpose commands: See what is not being used.

- Hardening for unit tests in cell logic: also combine cell namespaces.

- Transparent window idea.

- Configuration for visualization settings.

- Auto update on configuration file save.



# bugs

- exchange should also work reverse direction, exchange + exchange should undo the first operation.
-



# Done

- add swap and move operations to runRaylibUIMultiCluster function in @src/multi_ui.h.
key s selects the currently selected cell for the operation
key m(ove): moves the currently selected cell to previously selected cell
key e(xchange): swaps the currently selected cell to previously selected cell

key shift + s: clears the selected cell for operation

We need store the selected cell for operations in runRaylibUIMultiCluster as optional.
we need draw it a bit differently then other cells.

- update selected cell in the loop. check window activated, if it is from handle find the cluster and cell id and update the selected index.

- update mouse position after creating a new cell (new window): probably requires to do this after system update for delete as well. (delete seems to work well)

- Add more keyboard bindings

For the runLoopTestMode function in @src/loop.cpp I would like to add new shortcuts:

super + shift + ESC: escape loop, close app
super + shift + v: toggle global split dir, vertical horizontal

== Select cell for next operation (move or exchange)
super + shift + a: select currently selected cell for next operation, similar to S key in @src/multi_ui.xpp
super + shift + q: deselect currently selected cell for next operation, similar to shift + S key in @src/multi_ui.xpp
== Operate on currently selected cell
super + shift + e: exchange the currently selected cell with the cell that is selected for operation, similar to e key in @src/multi_ui.xpp
super + shift + m: move the cell selected to the operation using the currently selected cell as target, similar to m key in @src/multi_ui.xpp

- a loop action where we only list the current windows that passed the filter.

-- configuration file loading and initing