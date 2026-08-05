#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "fbterm.h"
#include "idle_timer.h"

IdleTimer::IdleTimer(u32 timeout, const s8 *command)
    : mTimeout(timeout), mCommand(strdup(command)), mActive(false), mTriggered(false)
{
    if (!mCommand) return;

    s32 idleFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (idleFd == -1) {
        perror("timerfd_create");
        return;
    }

    setFd(idleFd);
}

IdleTimer::~IdleTimer()
{
    free(mCommand);
}

bool IdleTimer::valid()
{
    return fd() != -1;
}

void IdleTimer::setActive(bool active)
{
    mActive = active;
    mTriggered = false;
    arm(active);
}

void IdleTimer::activity()
{
    if (!mActive) {
        return;
    }

    mTriggered = false;
    arm(true);
}

void IdleTimer::arm(bool enabled)
{
    if (fd() == -1) {
        return;
    }

    itimerspec timer = {};
    if (enabled) {
        timer.it_value.tv_sec = mTimeout;
    }

    if (timerfd_settime(fd(), 0, &timer, 0) == -1) {
        perror("timerfd_settime");
    }
}

void IdleTimer::runCommand()
{
    s32 pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    }

    if (pid) {
        return;
    }

    FbTerm::instance()->initChildProcess();
    execl("/bin/sh", "sh", "-c", mCommand, (s8*)0);

}

void IdleTimer::readyRead(s8 *buf, u32 len)
{
    if (len < sizeof(u64) || !mActive || mTriggered) {
        return;
    }

    mTriggered = true;
    runCommand();
}
