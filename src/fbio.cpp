/*
 *   Copyright © 2008 dragchan <zgchan317@gmail.com>
 *   This file is part of FbTerm.
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
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/epoll.h>
#include "fbio.h"

#define NR_EPOLL_FDS 10
#define NR_FDS 32

static IoPipe *ioPipeMap[NR_FDS];

IoDispatcher *IoDispatcher::createInstance()
{
	return new FbIoDispatcher();
}

FbIoDispatcher::FbIoDispatcher()
{
	mEpollFd = epoll_create(NR_EPOLL_FDS);
	fcntl(mEpollFd, F_SETFD, fcntl(mEpollFd, F_GETFD) | FD_CLOEXEC);
}

FbIoDispatcher::~FbIoDispatcher()
{
	for (u32 i = NR_FDS; i--;) {
		if (ioPipeMap[i]) delete ioPipeMap[i];
	}

	close(mEpollFd);
}

void FbIoDispatcher::addIoSource(IoPipe *src, bool isread)
{
	if (src->fd() >= NR_FDS) return;
	ioPipeMap[src->fd()] = src;

	epoll_event ev;
	ev.data.fd = src->fd();
	ev.events = (isread ? EPOLLIN : EPOLLOUT);
	epoll_ctl(mEpollFd, EPOLL_CTL_ADD, src->fd(), &ev);
}

void FbIoDispatcher::removeIoSource(IoPipe *src, bool isread)
{
	if (src->fd() >= NR_FDS) return;
	ioPipeMap[src->fd()] = 0;

	epoll_event ev;
	ev.data.fd = src->fd();
	ev.events = (isread ? EPOLLIN : EPOLLOUT);
	epoll_ctl(mEpollFd, EPOLL_CTL_DEL, src->fd(), &ev);

}

void FbIoDispatcher::poll()
{
	epoll_event evs[NR_EPOLL_FDS];
	s32 nfds = epoll_wait(mEpollFd, evs, NR_EPOLL_FDS, -1);

	for (s32 i = 0; i < nfds; i++) {
		IoPipe *src = ioPipeMap[evs[i].data.fd];
		if (!src) continue;

		if (evs[i].events & EPOLLIN) {
			src->ready(true);
		}

		if (evs[i].events & EPOLLOUT) {
			src->ready(false);
		}

		if (evs[i].events & EPOLLHUP) {
			delete src;
		}
	}
}
