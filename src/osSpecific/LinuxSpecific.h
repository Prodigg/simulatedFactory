//
// Created by prodigg on 07.06.26.
//

#ifndef SIMULATEDFACTORY_LINUXSPECIFIC_H
#define SIMULATEDFACTORY_LINUXSPECIFIC_H

#include <iostream>
#include <string>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
namespace sim::framework::internal {
    [[nodiscard]] inline bool line_available() {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        timespec timeout{};
        timeout.tv_sec = 0;
        timeout.tv_nsec = 0;

        sigset_t empty_mask;
        sigemptyset(&empty_mask);

        const int ret = pselect(
            STDIN_FILENO + 1,
            &set,
            nullptr,
            nullptr,
            &timeout,
            &empty_mask
        );

        return ret > 0;
    }

}

#endif //SIMULATEDFACTORY_LINUXSPECIFIC_H
