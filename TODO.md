# Architecture

 - Replace keyboard/mouse/output callbacks with event queue
 - Use bitset to track pending commit state
 - Weak wl_resource holder
 - Weak refcount / wl_resource arrays

# MVP Features

 - Z-ordering
 - Keyboard and pointer focus
 - Popups
 - Output globals
 - Subsurfaces
 - Data manager

# Extra

 - Set cursor
 - Transparency when blitting

# Bugs
