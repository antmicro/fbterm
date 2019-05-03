/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/vt.h>
#include <linux/version.h>
#include "fbterm.h"
#include "fbshell.h"
#include "fbconfig.h"
#include "fbio.h"
#include "screen.h"
#include "input.h"
#include "input_key.h"
#include "mouse.h"

#ifdef SYS_signalfd
#include <asm/types.h>
#include <linux/signalfd.h>

sigset_t oldSigmask;

class SignalIo : public IoPipe {
public:
	SignalIo(sigset_t &sigmask);

private:
	virtual void readyRead(s8 *buf, u32 len);
};

SignalIo::SignalIo(sigset_t &sigmask)
{
	int fd = syscall(SYS_signalfd, -1, &sigmask, 8);
	setFd(fd);
}

void SignalIo::readyRead(s8 *buf, u32 len)
{
	signalfd_siginfo *si = (signalfd_siginfo*)buf;
	for (len /= sizeof(*si); len--; si++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
		FbTerm::instance()->processSignal(si->signo);
#else
		FbTerm::instance()->processSignal(si->ssi_signo);
#endif
	}
}
#else

static sig_atomic_t pendsigs = 0;

static void signalHandler(s32 signo)
{
	pendsigs |= 1 << signo;
}

static void pollSignal()
{
	if (!pendsigs) return;
	
	sig_atomic_t sigs = pendsigs;
	pendsigs = 0;

	for (u32 i = 0; i < sizeof(sigs); i++) {
		u8 sig8 = (sigs >> (i * 8)) & 0xff;
		if (!sig8) continue;

		for (u32 j = 0; j < 8; j++) {
			if (sig8 & (1 << j)) {
				FbTerm::instance()->processSignal(i * 8 + j);
			}
		}
	}
}
#endif

DEFINE_INSTANCE_DEFAULT(FbTerm)
u32 effective_uid;

FbTerm::FbTerm()
{
	mInit = false;
	init();
}

FbTerm::~FbTerm()
{
	IoDispatcher::uninstance();
	Screen::uninstance();
	Config::uninstance();
}

void FbTerm::init()
{
	effective_uid = geteuid();
	seteuid(getuid());

	if (!TtyInput::instance() || !Screen::instance()) return;

	Mouse::instance();

	struct vt_mode vtm;
	vtm.mode = VT_PROCESS;
	vtm.waitv = 0;
	vtm.relsig = SIGUSR1;
	vtm.acqsig = SIGUSR2;
	vtm.frsig = 0;
	ioctl(STDIN_FILENO, VT_SETMODE, &vtm);

	sighandler_t sh;
#ifndef SYS_signalfd
	sh = signalHandler;
#else
	sh = SIG_IGN;

	sigset_t sigmask;
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGUSR1);
	sigaddset(&sigmask, SIGUSR2);
	sigaddset(&sigmask, SIGALRM);
	sigaddset(&sigmask, SIGTERM);

	sigprocmask(SIG_BLOCK, &sigmask, &oldSigmask);
	new SignalIo(sigmask);
#endif

	signal(SIGUSR1, sh);
	signal(SIGUSR2, sh);
	signal(SIGALRM, sh);
	signal(SIGTERM, sh);

	FbShell *shell = FbShell::createShell();
//	ioctl(STDIN_FILENO, TIOCCONS, shell->fd()); //should have perm CAP_SYS_ADMIN

	mInit = true;
}

void FbTerm::run()
{
	if (!mInit) return;

	mRun = true;
	FbIoDispatcher *io = (FbIoDispatcher*)IoDispatcher::instance();
	while (mRun) {
		io->poll();
#ifndef SYS_signalfd
		pollSignal();
#endif
	}
}

void FbTerm::processSignal(u32 signo)
{
	switch (signo) {
	case SIGTERM:
		exit();
		break;

	case SIGALRM:
		FbShell::drawCursor();
		break;

	case SIGUSR1:
		FbShell::enterLeaveVc(false);
		Screen::instance()->enterLeaveVc(false);
		TtyInput::instance()->enterLeaveVc(false);
		ioctl(STDIN_FILENO, VT_RELDISP, 1);
		break;

	case SIGUSR2:
		TtyInput::instance()->enterLeaveVc(true);
		Screen::instance()->enterLeaveVc(true);
		FbShell::enterLeaveVc(true);
		break;

	default:
		break;
	}
}

void FbTerm::processSysKey(u32 key)
{
	switch (key) {
	case CTRL_ALT_E:
		exit();
		break;

	case SHIFT_PAGEDOWN:
	case SHIFT_PAGEUP: {
		FbShell *shell = FbShell::activeShell();
		if (shell) {
			shell->historyDisplay(false, (key == SHIFT_PAGEDOWN) ? shell->h() : -shell->h());
		}
		break;
	}

	case CTRL_ALT_C:
		FbShell::createShell();
		break;

	case CTRL_ALT_D:
		FbShell::deleteShell();
		break;

	case CTRL_ALT_1 ... CTRL_ALT_0:
		FbShell::switchShell(key - CTRL_ALT_1);
		break;

	case SHIFT_LEFT:
		FbShell::prevShell();
		break;

	case SHIFT_RIGHT:
		FbShell::nextShell();
		break;

	case CTRL_ALT_F1 ... CTRL_ALT_F6: {
		FbShell *shell = FbShell::activeShell();
		if (shell) {
			shell->switchCodec(key - CTRL_ALT_F1);
		}
		break;
	}

	default:
		break;
	}
}

int main()
{
	FbTerm::instance()->run();
	FbTerm::uninstance();
	return 0;
}
