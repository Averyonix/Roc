#pragma once

#include "types.hpp"

bool wrei_allocate_shm_file_pair(usz size, int* p_rw_fd, int* p_ro_fd);
