- add swap and move operations to runRaylibUIMultiCluster function in @src/multi_ui.h.
key s selects the currently selected cell for the operation
key m(ove): moves the currently selected cell to previously selected cell
key e(xchange): swaps the currently selected cell to previously selected cell

key shift + s: clears the selected cell for operation

We need store the selected cell for operations in runRaylibUIMultiCluster as optional.
we need draw it a bit differently then other cells.

- update selected cell in the loop. check window activated, if it is from handle find the cluster and cell id and update the selected index.

- Add keyboard bindings