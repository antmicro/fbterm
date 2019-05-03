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
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/epoll.h>
#include "fbio.h"
#include "fbterm.h"

#define MAX_EPOLL_FDS 10

IoDispatcher *IoDispatcher::createInstance()
{
	return new FbIoDispatcher();
}

FbIoDispatcher::FbIoDispatcher()
{
	mpIoSources = new HashTable<IoPipe*>(MAX_EPOLL_FDS, true);
	mEpollFd = epoll_create(MAX_EPOLL_FDS);
	fcntl(mEpollFd, F_SETFD, fcntl(mEpollFd, F_GETFD) | FD_CLOEXEC);
}

FbIoDispatcher::~FbIoDispatcher()
{
	delete mpIoSources;
	close(mEpollFd);
}

void FbIoDispatcher::addIoSource(IoPipe *src, bool isread)
{
	epoll_event ev;
	ev.data.fd = src->fd();
	ev.events = (isread ? EPOLLIN : EPOLLOUT);
	epoll_ctl(mEpollFd, EPOLL_CTL_ADD, src->fd(), &ev);

	mpIoSources->add(src->fd(), src);
}

void FbIoDispatcher::removeIoSource(IoPipe *src, bool isread)
{
	epoll_event ev;
	ev.data.fd = src->fd();
	ev.events = (isread ? EPOLLIN : EPOLLOUT);
	epoll_ctl(mEpollFd, EPOLL_CTL_DEL, src->fd(), &ev);

	mpIoSources->remove(src->fd());
}

void FbIoDispatcher::poll()
{
	epoll_event evs[MAX_EPOLL_FDS];
	s32 nfds = epoll_wait(mEpollFd, evs, MAX_EPOLL_FDS, -1);

	IoPipe *src;
	for (s32 i = 0; i < nfds; i++) {
		mpIoSources->find(evs[i].data.fd, &src);
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
