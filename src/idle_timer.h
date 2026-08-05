#ifndef IDLE_TIMER_H
#define IDLE_TIMER_H

#include "io.h"

class IdleTimer : public IoPipe {
public:
    IdleTimer(u32 timeout, const s8* command);
    virtual ~IdleTimer();

    bool valid();
    void setActive(bool active);
    void activity();

private:
    void arm(bool enabled);
    void runCommand();
    virtual void readyRead(s8 *buf, u32 len);

    u32 mTimeout;
    s8 *mCommand;
    bool mActive;
    bool mTriggered;
};

#endif // IDLE_TIMER_H
