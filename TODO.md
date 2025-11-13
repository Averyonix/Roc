# To Do

 - Separate out protocol implementations
 - Pointer input
 - Basic client move/resize
 - Keyboard and pointer focus

# Bugs

- Running Roc inside of itself results in the nested instance getting stuck on `vkQueuePresentKHR` on the third present
   - This seems to happen regardless of how many images are requested in the swapchain
   - Haven't managed to replicate this with any other application yet
