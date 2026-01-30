#include "shm.hpp"

#include "util.hpp"

static
std::string wrei_random_shm_file_name()
{
    static std::mt19937 rng{std::default_random_engine{}()};
    static std::uniform_int_distribution<u64> dist{0, UINT64_MAX};
    return std::format("/" PROGRAM_NAME "-{}", dist(rng));
}

static
int wrei_excl_shm_open(std::string& name)
{
    for (int i = 0; i < 100; ++i) {
        name = wrei_random_shm_file_name();
        auto[fd, error] = unix_check(shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600), EEXIST);
        if (fd >= 0) return fd;
        if (error != EEXIST) break;
    }

    return -1;
}

bool wrei_allocate_shm_file_pair(usz size, int* p_rw_fd, int* p_ro_fd)
{
    std::string name;
    int rw_fd = wrei_excl_shm_open(name);
    if (rw_fd < 0) {
        return false;
    }

    int ro_fd = unix_check(shm_open(name.c_str(), O_RDONLY, 0)).value;
    if (ro_fd < 0) {
        shm_unlink(name.c_str());
        close(rw_fd);
        return false;
    }

    shm_unlink(name.c_str());

    if (fchmod(rw_fd, 0) != 0) {
        close(rw_fd);
        close(ro_fd);
        wrei_log_unix_error("allocate_shm_file_pair failed, file could be re-opened in read mode!");
        return false;
    }

    int ret;
    do {
        ret = ftruncate(rw_fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(rw_fd);
        close(ro_fd);
        return false;
    }

    *p_rw_fd = rw_fd;
    *p_ro_fd = ro_fd;
    return true;
}