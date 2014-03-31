/**@file Module for managing Linux signals and allowing multiple handlers per signal. */
/*
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "UnixSignal.h"
#include "Logger.h"

//UnixSignal gSigVec;

static void _sigHandler(int sig)
{
    signal(sig, SIG_IGN);
    if (sig <= 0 || sig >= UnixSignal::C_NSIG)
    {
	LOG(ERR) << "Signal Handler for signal " << sig << " (out of range)";
	return;
    }
    // work around C++ issue with function pointers and class based function pointers
    gSigVec.Handler(sig);
    signal(sig, SIG_DFL);
    printf("Rethrowing signal %d\n", sig);
    kill(getpid(), sig);
}

void UnixSignal::Handler(int sig)
{
    //TODO: Code should go here to generate the core file, before we change too
    // much of the data in the program.

    printf("Processing signal vector for sig %d\n", sig);
    mLock[sig].lock();
    for (std::list<sighandler_t>::iterator i = mListHandlers[sig].begin();
	i != mListHandlers[sig].end(); i++)
    {
    	(*i)(sig);
    }
    mLock[sig].unlock();
    printf("Done processing signal vector for sig %d\n", sig);
}

UnixSignal::UnixSignal(void)
{
    for (int i = 0; i < C_NSIG; i++)
    {
	mListHandlers[i].clear();
	signal(i, _sigHandler);
    }
}

UnixSignal::~UnixSignal(void)
{
    for (int i = 0; i < C_NSIG; i++)
    {
	mListHandlers[i].clear();
	signal(i, SIG_DFL);
    }
}

void UnixSignal::Register(sighandler_t handler, int sig) // register the handler to the signal
{
    if (sig <= 0 || sig >= C_NSIG)
    {
	LOG(ERR) << "Unable to register callback for UnixSignal " << sig << " (out of range)";
	return;
    }
    mLock[sig].lock();
    mListHandlers[sig].insert(mListHandlers[sig].end(), handler);
    mLock[sig].unlock();
}

void UnixSignal::Dump(void)
{
    for (int sig = 0; sig < C_NSIG; sig++)
    {
	mLock[sig].lock();
	if (mListHandlers[sig].size() != 0)
	{
	    printf("Signal vectors for signal %d: ", sig);
	    for (std::list<sighandler_t>::iterator i = mListHandlers[sig].begin();
		i != mListHandlers[sig].end(); i++)
	    {
		printf("%s0x%p", i == mListHandlers[sig].begin() ? "" : ", ", *i);
	    }
	    printf("\n");
	}
	mLock[sig].unlock();
    }
}