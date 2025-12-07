## Win-Tiler Cell System Plan

I would like to create a cell creation system.
A cell is a rectangle on the screen.

### High-level behavior

- The window starts with a single cell that fills the whole screen.
- At any time, there is exactly one **selected cell**, and **only leaf cells can be selected**.
- The user can create new cells by **dividing the currently selected cell**.
- The first division is **vertical** (left/right). Later divisions default to the current cell’s stored direction, which the user can change.

### Controls

- **h**: set the current cell’s division direction to **horizontal**.
- **v**: set the current cell’s division direction to **vertical**.
- **t**: toggle the current cell’s division direction between horizontal and vertical.
- **(split key, e.g. Enter/Space)**: split the currently selected **leaf** cell using its stored division direction.
- **d**: delete the currently selected **leaf** cell; its sibling fills the parent region.
- **Arrow keys**: move selection between **leaf** cells (only leaves can ever be selected).

### Cell tree model

- Cells form a **binary tree** over the window rectangle.
  - **Leaf cells**: visible rectangles that can be selected and drawn.
  - **Split cells**: internal nodes that store a split orientation and have exactly two children.
- The root node always represents the whole window.

### Core data structures (C++ / raylib-oriented)

- **Cell kind enum**
  - `enum class CellKind { Leaf, Split };`
  - `Leaf`: visible, selectable cell with no children.
  - `Split`: internal node that has two children and a split orientation; never directly selectable.

- **Split direction enum**
  - `enum class SplitDir { Vertical, Horizontal };`
  - `Vertical`: splits area into **left** and **right** child rectangles.
  - `Horizontal`: splits area into **top** and **bottom** child rectangles.
  - For a **leaf** cell: stores the preferred direction when that leaf is split next (changed by `h`, `v`, `t`).
  - For a **split** cell: stores the orientation currently used to divide its rectangle.

- **Cell struct (tree node)**
  - `CellKind kind;` — whether this node is a `Leaf` or `Split`.
  - `SplitDir splitDir;` — preferred/actual split orientation (see above).
  - `int parent;` — index of the parent in a `std::vector<Cell>`, or `-1` for root.
  - `int firstChild;` — index of first child (for `Split`), `-1` for leaves.
  - `int secondChild;` — index of second child (for `Split`), `-1` for leaves.
  - `Rectangle rect;` — screen-space rectangle for this node (raylib type).

- **Global/app state**
  - `std::vector<Cell> cells;` — storage for all nodes (leaves and splits).
  - `int rootIndex;` — index of the root node (usually `0`).
  - `int selectedIndex;` — index of the currently selected **leaf** node.

### Operations

- **Initialize**
  - Create a single root cell:
    - `kind = CellKind::Leaf`
    - `splitDir = SplitDir::Vertical` (first division is vertical).
    - `parent = -1`
    - `rect = {0, 0, (float)screenWidth, (float)screenHeight}`
  - Set `rootIndex = 0`, `selectedIndex = 0`.

- **Split selected leaf**
  - Works only if `selectedIndex` is a `Leaf`.
  - Read the leaf’s `splitDir` to decide between vertical or horizontal split.
  - Compute two child rectangles by cutting the leaf’s `rect` into two halves.
  - Turn the leaf into a `Split` node:
    - Keep its `rect` and `splitDir`.
    - Allocate two new `Leaf` cells as children with the computed rectangles and `parent = selectedIndex`.
    - Set `firstChild` and `secondChild` to these new indices.
  - Update selection: move `selectedIndex` to one of the new leaf children.

- **Toggle direction with `t`**
  - Only acts on the **selected leaf**.
  - Flip its `splitDir` between `Vertical` and `Horizontal`.
  - This only changes how it will be split the next time; it does not change existing geometry.

- **Delete selected leaf with `d`**
  - Works only if `selectedIndex` is a `Leaf` and not the root.
  - Let `p` be the parent index; let `sibling` be the other child of `p`.
  - Promote `sibling` into `p`’s place (copy over fields, fix parent links if needed).
  - After promotion, ensure `selectedIndex` points to a **leaf**:
    - If the promoted node is a leaf, select it.
    - If it is a split, select one of its leaf descendants (e.g., left/topmost).

- **Selection movement with arrow keys**
  - Selection is always on a **leaf**.
  - On each arrow key press, find another leaf cell in the requested direction:
    - For example, on Right arrow: among all leaves whose rectangles lie to the right of the current leaf, choose the nearest one.
  - Update `selectedIndex` to that leaf.

### Rendering and input (raylib integration)

- Each frame:
  - Handle input keys (`h`, `v`, `t`, split key, `d`, arrows) and modify the tree/state accordingly.
  - Draw all **leaf** cells as rectangles.
  - Highlight the cell at `selectedIndex` so the user can see the current selection.
