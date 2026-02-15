#include "wrio.hpp"

int main()
{
    auto wrio = wrio_context_create();
    wrio_context_run(wrio.get());
}
